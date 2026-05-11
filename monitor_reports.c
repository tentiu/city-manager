/* Activam standardul POSIX 2008 - necesar pentru sigaction(), kill(), getpid() etc. */
#define _POSIX_C_SOURCE 200809L
/* Activam extensiile X/Open - necesar pentru unele functii de timp */
#define _XOPEN_SOURCE   700


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define PID_FILE ".monitor_pid"

static volatile sig_atomic_t got_sigint  = 0;

static volatile sig_atomic_t got_sigusr1 = 0;

static void handle_sigint(int signo) {
    /* (void)signo suprima warning-ul "unused parameter" de la compilator */
    (void)signo;
    /* Setam flag-ul la 1 ca sa semnalam buclei principale sa se opreasca */
    got_sigint = 1;
}

static void handle_sigusr1(int signo) {
    /* Suprimam warning-ul pentru parametrul neutilizat */
    (void)signo;
    /* Semnalam buclei principale ca a venit o notificare de raport nou */
    got_sigusr1 = 1;
}

static void write_pid_file(void) {
    int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    /* Daca deschiderea a esuat, afisam eroarea si oprim programul */
    if (fd < 0) {
        perror("Cannot create .monitor_pid");
        exit(1); 
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    write(fd, buf, strlen(buf));
    close(fd);
    printf("[monitor] Started. PID=%d written to %s\n", (int)getpid(), PID_FILE);
    fflush(stdout);
}

static void remove_pid_file(void) {
    /* unlink() sterge un fisier de pe disk (sterge intrarea din director).
       Returneaza 0 la succes, -1 la eroare. */
    if (unlink(PID_FILE) == 0) {
        /* Fisierul a fost sters cu succes */
        printf("[monitor] Removed %s\n", PID_FILE);
    } else {
        /* unlink() a esuat - probabil fisierul nu mai exista */
        perror("[monitor] Could not remove .monitor_pid");
    }
}

int main(void) {
    printf("[monitor] monitor_reports starting...\n");
    fflush(stdout);
    write_pid_file();
    struct sigaction sa_int, sa_usr1;

    memset(&sa_int,  0, sizeof(sa_int));
    memset(&sa_usr1, 0, sizeof(sa_usr1));

    sa_int.sa_handler = handle_sigint;

    sigemptyset(&sa_int.sa_mask);

    sa_int.sa_flags = 0;
    
    if (sigaction(SIGINT, &sa_int, NULL) < 0) {
        perror("sigaction SIGINT");
        remove_pid_file(); /* curatam inainte sa iesim */
        exit(1);
    }

    /* Setam handler-ul pentru SIGUSR1 (acelasi procedeu ca pentru SIGINT) */
    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    /* Instaleaza handler-ul pentru SIGUSR1 */
    if (sigaction(SIGUSR1, &sa_usr1, NULL) < 0) {
        perror("sigaction SIGUSR1");
        remove_pid_file();
        exit(1);
    }

    /* Mesaj de confirmare ca handler-ele sunt instalate */
    printf("[monitor] Waiting for signals. Send SIGUSR1 to notify, SIGINT to stop.\n");
    fflush(stdout);

    while (!got_sigint) {
        pause();

        if (got_sigusr1) {
            /* Resetam flag-ul imediat la 0 pentru a putea detecta
               urmatorul semnal SIGUSR1 care va veni */
            got_sigusr1 = 0;

            time_t now = time(NULL);
        
            char timebuf[32];
           
            struct tm *t = localtime(&now);
            
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);

            printf("[monitor] [%s] New report added! (SIGUSR1 received)\n", timebuf);

            fflush(stdout);
        }

        if (got_sigint) break;
    }

    time_t now = time(NULL);
    char timebuf[32];
    struct tm *t = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);

    printf("[monitor] [%s] SIGINT received. Shutting down...\n", timebuf);
    fflush(stdout);

    /* Stergem fisierul .monitor_pid inainte de iesire */
    remove_pid_file();

    printf("[monitor] Goodbye.\n");
    fflush(stdout);

    return 0;
}
