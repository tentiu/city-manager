#define _POSIX_C_SOURCE 200809L
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

static volatile sig_atomic_t got_sigint  = 0; /* setat la SIGINT => oprire */
static volatile sig_atomic_t got_sigusr1 = 0; /* setat la SIGUSR1 => raport nou */

static void handle_sigint(int signo) {
    (void)signo; /* suprimam warning unused parameter */
    got_sigint = 1;
}

/* Handler SIGUSR1: seteaza flag-ul de notificare raport nou */
static void handle_sigusr1(int signo) {
    (void)signo;
    got_sigusr1 = 1;
}

static pid_t check_existing_monitor(void) {
    /* Incercam sa deschidem fisierul PID */
    int fd = open(PID_FILE, O_RDONLY);
    if (fd < 0) return -1; /* fisierul nu exista => niciun monitor nu ruleaza */

    /* Citim PID-ul din fisier */
    char buf[32];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1; /* fisier gol sau eroare */

    /* Convertim la pid_t */
    pid_t pid = (pid_t)atoi(buf);
    if (pid <= 0) return -1;

    /* kill(pid, 0) = verifica daca procesul cu acest PID exista.
       Returneaza 0 daca procesul exista, -1 daca nu (errno=ESRCH). */
    if (kill(pid, 0) == 0) {
        /* Procesul exista => alt monitor ruleaza deja */
        return pid;
    }
    /* Procesul nu mai exista (fisierul PID e stale/vechi) */
    return -1;
}

static void write_pid_file(void) {
    int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("ERROR:cannot create .monitor_pid\n");
        fflush(stdout);
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    write(fd, buf, strlen(buf));
    close(fd);

    printf("INFO:monitor started PID=%d\n", (int)getpid());
    fflush(stdout); /* flush imediat pentru ca hub_mon sa il primeasca */
}

/* Sterge fisierul .monitor_pid la oprire */
static void remove_pid_file(void) {
    unlink(PID_FILE);
    printf("SHUTDOWN:monitor stopped PID=%d\n", (int)getpid());
    fflush(stdout);
}

int main(void) {
    pid_t existing = check_existing_monitor();
    if (existing > 0) {
        /* Alt monitor ruleaza deja => trimitem eroare prin stdout (pipe)
           si iesim imediat fara sa scriem in .monitor_pid */
        printf("ERROR:monitor already running PID=%d\n", (int)existing);
        fflush(stdout);
        /* SHUTDOWN ca sa stie hub_mon ca am terminat */
        printf("SHUTDOWN:duplicate monitor exiting\n");
        fflush(stdout);
        return 1;
    }

    /* Scriem PID-ul nostru in fisier */
    write_pid_file();

    /* ── Instalarea handler-elor de semnale cu sigaction() ── */
    struct sigaction sa_int, sa_usr1;
    memset(&sa_int,  0, sizeof(sa_int));
    memset(&sa_usr1, 0, sizeof(sa_usr1));

    /* Handler pentru SIGINT (oprire) */
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) < 0) {
        printf("ERROR:sigaction SIGINT failed\n");
        fflush(stdout);
        remove_pid_file();
        exit(1);
    }

    /* Handler pentru SIGUSR1 (notificare raport nou) */
    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa_usr1, NULL) < 0) {
        printf("ERROR:sigaction SIGUSR1 failed\n");
        fflush(stdout);
        remove_pid_file();
        exit(1);
    }

    printf("INFO:monitor ready waiting for signals\n");
    fflush(stdout);

    /* ── Bucla principala ── */
    /* Asteptam semnale pana primim SIGINT */
    while (!got_sigint) {
        /* pause() suspenda procesul pana vine un semnal - eficient, fara CPU waste */
        pause();

        /* Verificam SIGUSR1 - raport nou adaugat */
        if (got_sigusr1) {
            got_sigusr1 = 0; /* resetam flag-ul pentru urmatorul semnal */

            /* Obtinem timestamp-ul curent */
            time_t now = time(NULL);
            char timebuf[32];
            struct tm *t = localtime(&now);
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);

            printf("NOTIFY:new report added at %s\n", timebuf);
            fflush(stdout); /* flush imediat - critic pentru citire din pipe */
        }

        if (got_sigint) break;
    }

    time_t now = time(NULL);
    char timebuf[32];
    struct tm *t = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);

    printf("SHUTDOWN:received SIGINT at %s\n", timebuf);
    fflush(stdout);

    /* Stergem fisierul PID */
    remove_pid_file();

    return 0;
}
