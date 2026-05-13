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


#define PID_FILE "monitor.pid"
#define PIPE_BUF_SIZE 1024
#define LINE_BUF_SIZE 512

static pid_t hub_mon_pid = -1;

static void hub_mon_process(void){
    int pipefd[2];
    if(pipe(pipefd)<0)
    {    perror("[hub_mon] pipe failed");
         exit(1);
    }

    pid_t monitor_pid = fork();
    if(monitor_pid < 0){
        perror("[hub_mon] fork failed");
        exit(1);
    }

    if(monitor_pid == 0){
        if(dup2(pipefd[1],STDOUT_FILENO)<0){
            perror("[monitor] dup2 failed");
            exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        char *args[] = {"./monitor_reports", NULL};
        execvp("./monitor_reports",args);

        perror("[monitor] execvp failed");
        exit(1);
    }
}
