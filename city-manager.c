#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#define NAME_LEN   64
#define CAT_LEN    32
#define DESC_LEN   128

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

static void reports_path(const char *district, char *out, size_t sz) {
    snprintf(out, sz, "%s/reports.dat", district);
}
static void cfg_path(const char *district, char *out, size_t sz) {
    snprintf(out, sz, "%s/district.cfg", district);
}
static void log_path(const char *district, char *out, size_t sz) {
    snprintf(out, sz, "%s/logged_district", district);
}
static void symlink_name(const char *district, char *out, size_t sz) {
    snprintf(out, sz, "active_reports-%s", district);
}

static void log_action(const char *district, const char *role,
                        const char *user, const char *action) {
    char lp[256];
    log_path(district, lp, sizeof(lp));
    //int fd = popen(lp, O_WRONLY | O_APPEND | O_CREAT, 0644);
    //if (fd < 0) return;
    chmod(lp, 0644);

    time_t now = time(NULL);
    char line[512];
    snprintf(line, sizeof(line), "%ld\t%s\t%s\t%s\n",
             (long)now, user, role, action);
    //fwrite(fd, line, strlen(line));
   // pclose(fd);
}

static int check_perm(const char *path, int role, int need_read, int need_write) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    mode_t m = st.st_mode;
    if (role == 0) {
        if (need_read  && !(m & S_IRUSR)) return 0;
        if (need_write && !(m & S_IWUSR)) return 0;
    } else {
        if (need_read  && !(m & S_IRGRP)) return 0;
        if (need_write && !(m & S_IWGRP)) return 0;
    }
    return 1;
}

int parse_condition(const char *input, char *field, char *op, char *value) {
    if (!input || !field || !op || !value) return 0;
    const char *first = strchr(input, ':');
    if (!first) return 0;
    size_t flen = (size_t)(first - input);
    if (flen == 0 || flen >= 32) return 0;
    strncpy(field, input, flen);
    field[flen] = '\0';
    const char *second = NULL;
    const char *p = first + 1;
    if ((p[0] == '<' || p[0] == '>' || p[0] == '!' || p[0] == '=') &&
         p[1] == '=') {
        op[0] = p[0]; op[1] = p[1]; op[2] = '\0';
        second = p + 2;
    } else if (p[0] == '<' || p[0] == '>') {
        op[0] = p[0]; op[1] = '\0';
        second = p + 1;
    } else {
        return 0;
    }
    if (second[0] != ':') return 0;
    second++;
    strncpy(value, second, 63);
    value[63] = '\0';
    return 1;
}

int main(int argc, char *argv[]) {
   char *role     = NULL;
    char *user     = NULL;
    char *command  = NULL;
    char *district = NULL;
    int   report_id = -1;
   int   threshold = -1;
    char *filter_conds[8];
    int   filter_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--role") == 0 && i+1 < argc) {
            role = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0 && i+1 < argc) {
            user = argv[++i];
        } else if (strcmp(argv[i], "--add") == 0 && i+1 < argc) {
            command = "add"; district = argv[++i];
        } else if (strcmp(argv[i], "--list") == 0 && i+1 < argc) {
            command = "list"; district = argv[++i];
        } else if (strcmp(argv[i], "--view") == 0 && i+2 < argc) {
            command = "view";
            district  = argv[++i];
            report_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--remove_report") == 0 && i+2 < argc) {
            command = "remove_report";
            district  = argv[++i];
            report_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--update_threshold") == 0 && i+2 < argc) {
            command = "update_threshold";
            district  = argv[++i];
            threshold = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--filter") == 0 && i+1 < argc) {
            command  = "filter";
            district = argv[++i];
            while (i+1 < argc && argv[i+1][0] != '-' && filter_count < 8) {
                filter_conds[filter_count++] = argv[++i];
            }
        }
    }

return 0;
}
