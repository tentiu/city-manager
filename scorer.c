
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#define NAME_LEN  64
#define CAT_LEN   32
#define DESC_LEN  128

typedef struct {
    int    id;
    char   inspector[NAME_LEN];
    float  lat;
    float  lon;
    char   category[CAT_LEN];
    int    severity;
    time_t timestamp;
    char   description[DESC_LEN];
} Report;

/* ─── Structura pentru scorul unui inspector ────────────────────────────── */
/* Tinem o lista de inspectori si scorurile lor */
#define MAX_INSPECTORS 64

typedef struct {
    char name[NAME_LEN]; /* numele inspectorului */
    int  score;          /* suma severtatilor rapoartelor sale */
    int  count;          /* numarul de rapoarte ale sale */
} InspectorScore;

int main(int argc, char *argv[]) {
    /* Verificam ca am primit exact un argument: numele districtului */
    if (argc != 2) {
        fprintf(stderr, "Usage: ./scorer <district_name>\n");
        return 1;
    }

    /* Numele districtului e primul argument */
    const char *district = argv[1];

    /* Construim calea catre reports.dat al districtului */
    char rp[256];
    snprintf(rp, sizeof(rp), "%s/reports.dat", district);

    /* Deschidem fisierul binar pentru citire */
    int fd = open(rp, O_RDONLY);
    if (fd < 0) {
        /* Daca districtul nu exista sau nu are rapoarte, scriem pe stdout
           (care e redirectat de hub prin pipe) */
        printf("SCORER_ERROR: cannot open %s\n", rp);
        fflush(stdout);
        return 1;
    }

    /* Array de scoruri per inspector */
    InspectorScore scores[MAX_INSPECTORS];
    /* Numarul de inspectori gasiti pana acum */
    int n_inspectors = 0;

    /* Citim fiecare raport din fisier */
    Report r;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        /* Cautam inspectorul in lista existenta */
        int found = 0;
        for (int i = 0; i < n_inspectors; i++) {
            if (strcmp(scores[i].name, r.inspector) == 0) {
                /* Inspectorul exista deja => adaugam severitatea la scorul sau */
                scores[i].score += r.severity;
                scores[i].count++;
                found = 1;
                break;
            }
        }
        if (!found && n_inspectors < MAX_INSPECTORS) {
            /* Inspector nou => il adaugam in lista */
            strncpy(scores[n_inspectors].name, r.inspector, NAME_LEN - 1);
            scores[n_inspectors].name[NAME_LEN - 1] = '\0';
            scores[n_inspectors].score = r.severity;
            scores[n_inspectors].count = 1;
            n_inspectors++;
        }
    }
    close(fd);

    /* Scriem rezultatele pe stdout (redirectat de hub prin dup2+pipe).
       Formatul: SCORER_RESULT:<district>:<inspector>:<score>:<count>
       Prefixul SCORER_RESULT: permite hub-ului sa identifice tipul mesajului. */
    printf("SCORER_START:%s\n", district);
    fflush(stdout);

    if (n_inspectors == 0) {
        /* Nu am gasit niciun raport in district */
        printf("SCORER_EMPTY:%s:no reports found\n", district);
        fflush(stdout);
    } else {
        /* Afisam scorul fiecarui inspector */
        for (int i = 0; i < n_inspectors; i++) {
            /* Format: SCORER_RESULT:<district>:<inspector>:<total_score>:<nr_rapoarte> */
            printf("SCORER_RESULT:%s:%s:%d:%d\n",
                   district,
                   scores[i].name,
                   scores[i].score,
                   scores[i].count);
            fflush(stdout);
        }
    }

    /* Marcam sfarsitul outputului pentru acest district */
    printf("SCORER_END:%s\n", district);
    fflush(stdout);

    return 0;
}
