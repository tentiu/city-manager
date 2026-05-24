

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>  
#include <signal.h>    
#include <time.h>
#include <errno.h>

/* Fisierul PID al monitorului (acelasi ca in monitor_reports.c) */
#define PID_FILE        ".monitor_pid"
/* Dimensiunea bufferului pentru citirea din pipe */
#define PIPE_BUF_SIZE   1024
/* Dimensiunea maxima a unei linii citite din pipe */
#define LINE_BUF_SIZE   512

static pid_t hub_mon_pid = -1;

static void hub_mon_process(void) {
    /* ── Cream pipe-ul ── */
    /* pipe(fds) creeaza un pipe:
       fds[0] = capatul de CITIRE (read end)
       fds[1] = capatul de SCRIERE (write end)
       Datele scrise in fds[1] pot fi citite din fds[0]. */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("[hub_mon] pipe failed");
        exit(1);
    }

    /* ── Fork-uim procesul monitor_reports ── */
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        perror("[hub_mon] fork failed");
        exit(1);
    }

    if (monitor_pid == 0) {
        /* ── Suntem in procesul MONITOR (copilul hub_mon) ── */

        /* Redirectam stdout-ul monitorului catre capatul de scriere al pipe-ului.
           dup2(sursa, destinatie) copiaza file descriptor-ul sursa peste destinatie.
           STDOUT_FILENO = 1 (file descriptor-ul standard pentru stdout)
           Dupa dup2: scrierile pe stdout (fd 1) merg de fapt in pipe (fds[1]) */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("[monitor] dup2 failed");
            exit(1);
        }

        /* Inchidem ambele capete ale pipe-ului in procesul copil.
           Le-am duplicat cu dup2, deci originalele nu mai sunt necesare.
           Daca nu le inchidem, pipe-ul nu se va inchide corect la EOF. */
        close(pipefd[0]); /* inchidem capatul de citire (nu il folosim in monitor) */
        close(pipefd[1]); /* inchidem originalul (acum stdout e redirectat) */

        /* Inlocuim imaginea procesului cu monitor_reports.
           execvp cauta monitor_reports in directorul curent si in PATH. */
        char *args[] = { "./monitor_reports", NULL };
        execvp("./monitor_reports", args);

        /* Daca execvp returneaza, a esuat */
        perror("[monitor] execvp failed");
        exit(1);
    }

    /* ── Suntem in procesul HUB_MON ── */

    /* Inchidem capatul de scriere al pipe-ului in hub_mon.
       hub_mon doar citeste din pipe, nu scrie.
       Daca nu inchidem capatul de scriere, read() nu va returna EOF niciodata
       (pentru ca exista inca un fd deschis pentru scriere). */
    close(pipefd[1]);

    /* ── Citim mesajele monitorului din pipe ── */
    /* Citim linie cu linie folosind read() */
    char line[LINE_BUF_SIZE];
    int  line_len = 0;
    char ch;

    /* Bucla principala de citire din pipe */
    while (1) {
        /* Citim un caracter la un moment dat din pipe.
           read() blocheaza pana are date disponibile sau pipe-ul e inchis. */
        ssize_t n = read(pipefd[0], &ch, 1);

        if (n <= 0) {
            /* n == 0: pipe-ul a fost inchis (monitorul s-a terminat)
               n < 0: eroare la citire
               In ambele cazuri iesim din bucla. */
            break;
        }

        if (ch == '\n' || line_len >= LINE_BUF_SIZE - 1) {
            /* Am ajuns la sfarsitul unei linii => o procesam */
            line[line_len] = '\0'; /* terminam sirul */
            line_len = 0;          /* resetam pentru urmatoarea linie */

            /* Procesam linia in functie de prefix (format: TIP:mesaj) */
            if (strncmp(line, "INFO:", 5) == 0) {
                printf("[monitor] %s\n", line + 5);
                fflush(stdout);
            } else if (strncmp(line, "NOTIFY:", 7) == 0) {
                printf("[monitor] ** NEW REPORT: %s\n", line + 7);
                fflush(stdout);
            } else if (strncmp(line, "ERROR:", 6) == 0) {
               
                printf("[monitor] ERROR: %s\n", line + 6);
                fflush(stdout);
            } else if (strncmp(line, "SHUTDOWN:", 9) == 0) {
            
                printf("[monitor] Monitor has ended: %s\n", line + 9);
                fflush(stdout);
                /* Dupa SHUTDOWN, monitorul nu mai trimite nimic => iesim */
                break;
            } else {
                /* Mesaj necunoscut => il afisam oricum */
                printf("[monitor] %s\n", line);
                fflush(stdout);
            }
        } else {
            /* Acumulam caracterul in buffer */
            line[line_len++] = ch;
        }
    }

    /* Inchidem capatul de citire */
    close(pipefd[0]);

    /* Asteptam ca procesul monitor sa se termine */
    int status;
    waitpid(monitor_pid, &status, 0);

    /* Mesaj final catre utilizator */
    printf("[hub_mon] Monitor process ended. hub_mon exiting.\n");
    fflush(stdout);

    /* hub_mon iese */
    exit(0);
}

/* cmd_start_monitor: comanda "start_monitor" din interfata hub-ului.
   Creeaza procesul hub_mon care la randul sau porneste monitor_reports. */
static void cmd_start_monitor(void) {
    /* Verificam daca hub_mon ruleaza deja */
    if (hub_mon_pid > 0) {
        /* Verificam daca procesul exista inca cu kill(pid, 0) */
        if (kill(hub_mon_pid, 0) == 0) {
            printf("[hub] Monitor is already running (hub_mon PID=%d).\n", hub_mon_pid);
            return;
        }
        /* Procesul nu mai exista => resetam */
        hub_mon_pid = -1;
    }

    printf("[hub] Starting monitor...\n");
    fflush(stdout);

    /* Fork-uim hub_mon */
    pid_t pid = fork();
    if (pid < 0) {
        perror("[hub] fork hub_mon failed");
        return;
    }

    if (pid == 0) {
        /* ── Suntem in procesul HUB_MON ── */
        /* Apelam functia care implementeaza logica hub_mon */
        hub_mon_process();
        /* hub_mon_process() apeleaza exit() intern, nu ajungem niciodata aici */
        exit(0);
    }

    /* ── Suntem in procesul HUB (parinte) ── */
    /* Retinem PID-ul hub_mon pentru a-l putea opri mai tarziu */
    hub_mon_pid = pid;
    printf("[hub] hub_mon started with PID=%d.\n", hub_mon_pid);
    printf("[hub] Monitor output will appear below as it arrives.\n");
}

static void cmd_calculate_scores(char **districts, int n_districts) {
    if (n_districts == 0) {
        printf("[hub] Usage: calculate_scores <district1> [district2] ...\n");
        return;
    }

    printf("[hub] Calculating workload scores for %d district(s)...\n", n_districts);
    printf("[hub] ========================================\n");
    fflush(stdout);

    /* Procesam fiecare district separat */
    for (int d = 0; d < n_districts; d++) {
        const char *district = districts[d];

        /* ── Cream pipe-ul pentru acest scorer ── */
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            perror("[hub] pipe failed");
            continue; /* trecem la urmatorul district */
        }

        /* ── Fork-uim procesul scorer ── */
        pid_t scorer_pid = fork();
        if (scorer_pid < 0) {
            perror("[hub] fork scorer failed");
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (scorer_pid == 0) {
            /* ── Suntem in procesul SCORER (copil) ── */

            /* Redirectam stdout-ul scorer-ului catre capatul de scriere al pipe-ului.
               dup2(pipefd[1], STDOUT_FILENO):
               - STDOUT_FILENO (=1) va deveni o copie a pipefd[1]
               - Orice printf/write pe stdout va ajunge de fapt in pipe */
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                perror("[scorer] dup2 failed");
                exit(1);
            }

            /* Inchidem ambele capete originale ale pipe-ului in scorer.
               Acum stdout e redirectat, deci originalele nu mai sunt necesare. */
            close(pipefd[0]); /* read end - nu il folosim in scorer */
            close(pipefd[1]); /* write end - acum e duplicat in stdout */

            /* Executam scorer-ul cu numele districtului ca argument.
               execvp cauta ./scorer in directorul curent. */
            char *args[] = { "./scorer", (char *)district, NULL };
            execvp("./scorer", args);

            /* Daca execvp returneaza, a esuat */
            perror("[scorer] execvp failed");
            exit(1);
        }


        /* Inchidem capatul de scriere in hub.
           Hub-ul doar citeste din pipe, nu scrie.
           Daca nu inchidem pipefd[1], read() nu va returna EOF. */
        close(pipefd[1]);

        /* ── Citim outputul scorer-ului din pipe ── */
        char line[LINE_BUF_SIZE];
        int  line_len = 0;
        char ch;

        /* Citim linie cu linie din pipe */
        while (1) {
            ssize_t n = read(pipefd[0], &ch, 1);
            if (n <= 0) break; /* EOF sau eroare */

            if (ch == '\n' || line_len >= LINE_BUF_SIZE - 1) {
                line[line_len] = '\0';
                line_len = 0;

                /* Procesam linia in functie de prefix */
                if (strncmp(line, "SCORER_START:", 13) == 0) {
                    /* Inceputul rezultatelor pentru un district */
                    printf("[hub] --- District: %s ---\n", line + 13);
                    fflush(stdout);
                } else if (strncmp(line, "SCORER_RESULT:", 14) == 0) {
                    /* Format: SCORER_RESULT:<district>:<inspector>:<score>:<count>
                       Parsam campurile separate prin ':' */
                    char dist_name[64], insp_name[64];
                    int score, count;
                    if (sscanf(line + 14, "%63[^:]:%63[^:]:%d:%d",
                               dist_name, insp_name, &score, &count) == 4) {
                        printf("[hub]   Inspector: %-20s | Score: %3d | Reports: %d\n",
                               insp_name, score, count);
                        fflush(stdout);
                    }
                } else if (strncmp(line, "SCORER_EMPTY:", 13) == 0) {
                    /* Districtul nu are rapoarte */
                    printf("[hub]   (no reports in this district)\n");
                    fflush(stdout);
                } else if (strncmp(line, "SCORER_END:", 11) == 0) {
                    /* Sfarsitul rezultatelor pentru acest district */
                    printf("[hub] --- End of district: %s ---\n", line + 11);
                    fflush(stdout);
                } else if (strncmp(line, "SCORER_ERROR:", 13) == 0) {
                    /* Eroare la scorer */
                    printf("[hub] ERROR: %s\n", line + 13);
                    fflush(stdout);
                }
                /* Liniile goale le ignoram */
            } else {
                line[line_len++] = ch;
            }
        }

        /* Inchidem capatul de citire */
        close(pipefd[0]);

        /* Asteptam ca scorer-ul sa se termine */
        int status;
        waitpid(scorer_pid, &status, 0);
    }

    printf("[hub] ========================================\n");
    printf("[hub] Score calculation complete.\n");
    fflush(stdout);
}


/* cmd_stop_monitor: trimite SIGINT catre monitor pentru a-l opri */
static void cmd_stop_monitor(void) {
    /* Citim PID-ul monitorului din .monitor_pid */
    int fd = open(PID_FILE, O_RDONLY);
    if (fd < 0) {
        printf("[hub] No monitor is running (.monitor_pid not found).\n");
        return;
    }
    char buf[32];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf) - 1);
    close(fd);

    pid_t pid = (pid_t)atoi(buf);
    if (pid <= 0) {
        printf("[hub] Invalid PID in .monitor_pid.\n");
        return;
    }

    /* Trimitem SIGINT catre monitor */
    if (kill(pid, SIGINT) == 0) {
        printf("[hub] Sent SIGINT to monitor (PID=%d).\n", pid);
    } else {
        printf("[hub] Failed to send SIGINT to monitor (PID=%d): %s\n",
               pid, strerror(errno));
    }
}

static int parse_command(char *line, char **cmd, char args[][64], int max_args) {
    /* Stergem newline-ul de la sfarsit daca exista */
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

    /* Primul token e comanda */
    *cmd = strtok(line, " \t");
    if (*cmd == NULL) return 0; /* linie goala */

    /* Tokenizare argumente */
    int n = 0;
    char *token;
    while ((token = strtok(NULL, " \t")) != NULL && n < max_args) {
        strncpy(args[n], token, 63);
        args[n][63] = '\0';
        n++;
    }
    return n; /* numarul de argumente */
}

int main(void) {
    printf("============================================\n");
    printf("  city_hub - City Infrastructure Manager   \n");
    printf("  Phase 3 Interactive Interface             \n");
    printf("============================================\n");
    printf("Commands:\n");
    printf("  start_monitor                    - Start background monitor\n");
    printf("  stop_monitor                     - Stop the monitor\n");
    printf("  calculate_scores <d1> [d2] ...   - Calculate inspector workload\n");
    printf("  quit / exit                      - Exit hub\n");
    printf("============================================\n\n");
    fflush(stdout);

    /* Buffer pentru linia de comanda introdusa de utilizator */
    char line[512];
    /* Buffer pentru comanda extrasa */
    char *cmd;
    /* Array pentru argumentele comenzii */
    char args[16][64];

    /* ── Bucla principala de citire a comenzilor ── */
    while (1) {
        /* Afisam promptul */
        printf("city_hub> ");
        fflush(stdout);

        /* Citim linia de comanda de la stdin.
           fgets returneaza NULL la EOF (Ctrl+D) sau eroare. */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF => iesim din hub */
            printf("\n[hub] EOF received. Exiting.\n");
            break;
        }

        /* Parsam comanda si argumentele */
        int n_args = parse_command(line, &cmd, args, 16);

        /* Daca linia e goala, continuam */
        if (cmd == NULL || strlen(cmd) == 0) continue;

        /* ── Procesam comanda ── */
        if (strcmp(cmd, "start_monitor") == 0) {
            /* Porneste monitorul prin hub_mon */
            cmd_start_monitor();

        } else if (strcmp(cmd, "stop_monitor") == 0) {
            /* Opreste monitorul prin SIGINT */
            cmd_stop_monitor();

        } else if (strcmp(cmd, "calculate_scores") == 0) {
            /* Calculeaza scorurile pentru districtele date ca argumente */
            if (n_args == 0) {
                printf("[hub] Usage: calculate_scores <district1> [district2] ...\n");
            } else {
                /* Construim array-ul de pointeri catre numele districtelor */
                char *districts[16];
                for (int i = 0; i < n_args; i++) {
                    districts[i] = args[i];
                }
                cmd_calculate_scores(districts, n_args);
            }

        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            /* Iesim din hub */
            printf("[hub] Exiting city_hub. Goodbye!\n");

            /* Daca hub_mon ruleaza, il oprim inainte de iesire */
            if (hub_mon_pid > 0 && kill(hub_mon_pid, 0) == 0) {
                printf("[hub] Stopping hub_mon (PID=%d)...\n", hub_mon_pid);
                /* Oprim mai intai monitorul */
                cmd_stop_monitor();
                /* Asteptam putin ca monitorul sa se opreasca */
                sleep(1);
                /* Oprim hub_mon */
                kill(hub_mon_pid, SIGTERM);
                waitpid(hub_mon_pid, NULL, WNOHANG);
            }
            break;

        } else {
            printf("[hub] Unknown command: '%s'. Type 'quit' to exit.\n", cmd);
        }

        if (hub_mon_pid > 0) {
            int status;
            pid_t result = waitpid(hub_mon_pid, &status, WNOHANG);
            if (result == hub_mon_pid) {
                /* hub_mon s-a terminat */
                printf("[hub] hub_mon (PID=%d) has exited.\n", hub_mon_pid);
                hub_mon_pid = -1;
            }
        }
    }

    return 0;
}
