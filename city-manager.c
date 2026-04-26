#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>

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


/* Convert permission bits to rwxrwxrwx string (9 chars + NUL). */
static void mode_to_str(mode_t m, char *out) {
    out[0] = (m & S_IRUSR) ? 'r' : '-';
    out[1] = (m & S_IWUSR) ? 'w' : '-';
    out[2] = (m & S_IXUSR) ? 'x' : '-';
    out[3] = (m & S_IRGRP) ? 'r' : '-';
    out[4] = (m & S_IWGRP) ? 'w' : '-';
    out[5] = (m & S_IXGRP) ? 'x' : '-';
    out[6] = (m & S_IROTH) ? 'r' : '-';
    out[7] = (m & S_IWOTH) ? 'w' : '-';
    out[8] = (m & S_IXOTH) ? 'x' : '-';
    out[9] = '\0';
}

/* Build path strings */
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

/* Append a line to the operation log */
static void log_action(const char *district, const char *role,
                        const char *user, const char *action) {
    char lp[256];
    log_path(district, lp, sizeof(lp));
    int fd = open(lp, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    chmod(lp, 0644);

    time_t now = time(NULL);
    char line[512];
    snprintf(line, sizeof(line), "%ld\t%s\t%s\t%s\n",
             (long)now, user, role, action);
    write(fd, line, strlen(line));
    close(fd);
}

/* Check whether a permission bit matches what the role needs.
   role 0 = manager (owner), role 1 = inspector (group).
   Returns 1 if allowed, 0 if denied. */
static int check_perm(const char *path, int role, int need_read, int need_write) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    mode_t m = st.st_mode;
    if (role == 0) { /* manager = owner bits */
        if (need_read  && !(m & S_IRUSR)) return 0;
        if (need_write && !(m & S_IWUSR)) return 0;
    } else { /* inspector = group bits */
        if (need_read  && !(m & S_IRGRP)) return 0;
        if (need_write && !(m & S_IWGRP)) return 0;
    }
    return 1;
}

/* Create district directory + files if they don't exist */
static void ensure_district(const char *district) {
    struct stat st;
    if (stat(district, &st) < 0) {
        mkdir(district, 0750);
        chmod(district, 0750);
    }

    char rp[256], cp[256], lp[256];
    reports_path(district, rp, sizeof(rp));
    cfg_path(district, cp, sizeof(cp));
    log_path(district, lp, sizeof(lp));

    /* reports.dat */
    if (stat(rp, &st) < 0) {
        int fd = open(rp, O_CREAT | O_WRONLY, 0664);
        if (fd >= 0) close(fd);
        chmod(rp, 0664);
    }

    /* district.cfg – write default threshold if new */
    if (stat(cp, &st) < 0) {
        int fd = open(cp, O_CREAT | O_WRONLY, 0640);
        if (fd >= 0) {
            const char *def = "severity_threshold=2\n";
            write(fd, def, strlen(def));
            close(fd);
        }
        chmod(cp, 0640);
    }

    /* logged_district */
    if (stat(lp, &st) < 0) {
        int fd = open(lp, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) close(fd);
        chmod(lp, 0644);
    }

    /* Symbolic link active_reports-<district> -> reports.dat */
    char sl[256], target[512];
    symlink_name(district, sl, sizeof(sl));
    snprintf(target, sizeof(target), "%s/reports.dat", district);

    struct stat lst;
    if (lstat(sl, &lst) < 0) {
        symlink(target, sl);
    }
}

/* Next report ID = max existing + 1 */
static int next_id(const char *district) {
    char rp[256];
    reports_path(district, rp, sizeof(rp));
    int fd = open(rp, O_RDONLY);
    if (fd < 0) return 1;
    int max_id = 0;
    Report r;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.id > max_id) max_id = r.id;
    }
    close(fd);
    return max_id + 1;
}

static void op_add(const char *district, const char *role,
                   const char *user, int is_manager) {
    ensure_district(district);

    char rp[256];
    reports_path(district, rp, sizeof(rp));

    /* Both roles may add; check write access on reports.dat for the role */
    if (!check_perm(rp, is_manager ? 0 : 1, 0, 1)) {
        fprintf(stderr, "Permission denied: %s cannot write to %s\n", role, rp);
        return;
    }

    Report r;
    memset(&r, 0, sizeof(r));

    r.id = next_id(district);
    strncpy(r.inspector, user, NAME_LEN - 1);
    r.timestamp = time(NULL);

    printf("X: "); fflush(stdout); scanf("%f", &r.lat);
    printf("Y: "); fflush(stdout); scanf("%f", &r.lon);
    printf("Category (road/lighting/flooding/other): "); fflush(stdout);
    scanf("%31s", r.category);
    printf("Severity level (1/2/3): "); fflush(stdout); scanf("%d", &r.severity);
    printf("Description: "); fflush(stdout); getchar();
    fgets(r.description, DESC_LEN, stdin);
   
    size_t dl = strlen(r.description);
    if (dl > 0 && r.description[dl-1] == '\n') r.description[dl-1] = '\0';

    int fd = open(rp, O_WRONLY | O_APPEND);
    if (fd < 0) { perror("open reports.dat"); return; }
    write(fd, &r, sizeof(r));
    close(fd);
    chmod(rp, 0664);

    log_action(district, role, user, "add");
    printf("Report %d added to district '%s'.\n", r.id, district);
}

static void op_list(const char *district, const char *role,
                    const char *user) {
    char rp[256];
    reports_path(district, rp, sizeof(rp));

    struct stat st;
    if (stat(rp, &st) < 0) { fprintf(stderr, "District '%s' not found.\n", district); return; }

    char perms[10];
    mode_to_str(st.st_mode & 07777, perms);
    char timebuf[64];
    struct tm *tm_info = localtime(&st.st_mtime);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("reports.dat  permissions: %s  size: %lld bytes  last modified: %s\n\n",
           perms, (long long)st.st_size, timebuf);

    int fd = open(rp, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return; }

    Report r;
    int count = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        char ts[32];
        struct tm *t = localtime(&r.timestamp);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
        printf("ID: %d | Inspector: %s | Lat: %.4f | Lon: %.4f | "
               "Category: %s | Severity: %d | Time: %s\n",
               r.id, r.inspector, r.lat, r.lon,
               r.category, r.severity, ts);
        count++;
    }
    close(fd);
    if (count == 0) printf("(no reports)\n");

    log_action(district, role, user, "list");
}

static void op_view(const char *district, int report_id,
                    const char *role, const char *user) {
    char rp[256];
    reports_path(district, rp, sizeof(rp));
    int fd = open(rp, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "District '%s' not found.\n", district); return; }

    Report r;
    int found = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.id == report_id) {
            found = 1;
            char ts[32];
            struct tm *t = localtime(&r.timestamp);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
            printf("=== Report %d ===\n", r.id);
            printf("Inspector  : %s\n", r.inspector);
            printf("Location   : %.6f, %.6f\n", r.lat, r.lon);
            printf("Category   : %s\n", r.category);
            printf("Severity   : %d\n", r.severity);
            printf("Timestamp  : %s\n", ts);
            printf("Description: %s\n", r.description);
            break;
        }
    }
    close(fd);
    if (!found) fprintf(stderr, "Report %d not found in district '%s'.\n", report_id, district);

    log_action(district, role, user, "view");
}

static void op_remove_report(const char *district, int report_id,
                              const char *role, const char *user, int is_manager) {
    if (!is_manager) {
        fprintf(stderr, "Permission denied: only managers can remove reports.\n");
        return;
    }

    char rp[256];
    reports_path(district, rp, sizeof(rp));

    int fd = open(rp, O_RDWR);
    if (fd < 0) { perror("open reports.dat"); return; }

    struct stat st;
    fstat(fd, &st);
    int n = (int)(st.st_size / sizeof(Report));

    /* Find index of report */
    int idx = -1;
    Report r;
    for (int i = 0; i < n; i++) {
        lseek(fd, (off_t)(i * sizeof(Report)), SEEK_SET);
        read(fd, &r, sizeof(r));
        if (r.id == report_id) { idx = i; break; }
    }

    if (idx < 0) {
        fprintf(stderr, "Report %d not found.\n", report_id);
        close(fd);
        return;
    }

    /* Shift records after idx one position to the left */
    for (int i = idx + 1; i < n; i++) {
        lseek(fd, (off_t)(i * sizeof(Report)), SEEK_SET);
        read(fd, &r, sizeof(r));
        lseek(fd, (off_t)((i - 1) * sizeof(Report)), SEEK_SET);
        write(fd, &r, sizeof(r));
    }

    /* Truncate file by one record */
    ftruncate(fd, (off_t)((n - 1) * sizeof(Report)));
    close(fd);

    /* Verify with stat */
    stat(rp, &st);
    printf("Report %d removed. File now %lld bytes (%d records).\n",
           report_id, (long long)st.st_size, n - 1);

    log_action(district, role, user, "remove_report");
}

static void op_update_threshold(const char *district, int value,
                                 const char *role, const char *user, int is_manager) {
    if (!is_manager) {
        fprintf(stderr, "Permission denied: only managers can update threshold.\n");
        return;
    }

    char cp[256];
    cfg_path(district, cp, sizeof(cp));

    /* Verify permission bits are exactly 640 */
    struct stat st;
    if (stat(cp, &st) < 0) { perror("stat district.cfg"); return; }
    mode_t expected = S_IRUSR | S_IWUSR | S_IRGRP; /* 640 */
    if ((st.st_mode & 0777) != expected) {
        fprintf(stderr, "Security check failed: district.cfg permissions changed "
                "(expected 640, got %03o). Refusing to write.\n",
                (unsigned)(st.st_mode & 0777));
        return;
    }

    int fd = open(cp, O_WRONLY | O_TRUNC);
    if (fd < 0) { perror("open district.cfg"); return; }
    char buf[64];
    snprintf(buf, sizeof(buf), "severity_threshold=%d\n", value);
    write(fd, buf, strlen(buf));
    close(fd);

    printf("Threshold updated to %d in district '%s'.\n", value, district);
    log_action(district, role, user, "update_threshold");
}

/* ─── AI-assisted filter functions ──────────────────────────────────────── */

/*
 * parse_condition: split "field:operator:value" into its three parts.
 * Generated with AI assistance (see ai_usage.md), reviewed and adapted.
 * Returns 1 on success, 0 on failure.
 */
int parse_condition(const char *input, char *field, char *op, char *value) {
    if (!input || !field || !op || !value) return 0;

    const char *first = strchr(input, ':');
    if (!first) return 0;

    size_t flen = (size_t)(first - input);
    if (flen == 0 || flen >= 32) return 0;
    strncpy(field, input, flen);
    field[flen] = '\0';

    const char *second = NULL;
    /* operator can be ==, !=, <=, >=, <, > */
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

/*
 * match_condition: return 1 if record r satisfies field:op:value.
 * Generated with AI assistance (see ai_usage.md), reviewed and adapted.
 * The AI left type conversion partly incomplete; integer conversion added manually.
 */
int match_condition(Report *r, const char *field, const char *op, const char *value) {
    if (!r || !field || !op || !value) return 0;

    /* severity – integer comparison */
    if (strcmp(field, "severity") == 0) {
        int v = atoi(value);
        if (strcmp(op, "==") == 0) return r->severity == v;
        if (strcmp(op, "!=") == 0) return r->severity != v;
        if (strcmp(op, "<")  == 0) return r->severity <  v;
        if (strcmp(op, "<=") == 0) return r->severity <= v;
        if (strcmp(op, ">")  == 0) return r->severity >  v;
        if (strcmp(op, ">=") == 0) return r->severity >= v;
    }

    /* timestamp – integer comparison */
    if (strcmp(field, "timestamp") == 0) {
        long long v = atoll(value);
        long long ts = (long long)r->timestamp;
        if (strcmp(op, "==") == 0) return ts == v;
        if (strcmp(op, "!=") == 0) return ts != v;
        if (strcmp(op, "<")  == 0) return ts <  v;
        if (strcmp(op, "<=") == 0) return ts <= v;
        if (strcmp(op, ">")  == 0) return ts >  v;
        if (strcmp(op, ">=") == 0) return ts >= v;
    }

    /* category – string comparison (only == and != make sense) */
    if (strcmp(field, "category") == 0) {
        int cmp = strcmp(r->category, value);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;
        return 0;
    }

    /* inspector – string comparison */
    if (strcmp(field, "inspector") == 0) {
        int cmp = strcmp(r->inspector, value);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;
        return 0;
    }

    return 0;
}

static void op_filter(const char *district, int cond_count,
                       char **conditions, const char *role, const char *user) {
    char rp[256];
    reports_path(district, rp, sizeof(rp));

    /* Parse all conditions first */
    char fields[8][32], ops[8][4], values[8][64];
    for (int i = 0; i < cond_count; i++) {
        if (!parse_condition(conditions[i], fields[i], ops[i], values[i])) {
            fprintf(stderr, "Invalid condition: '%s'\n", conditions[i]);
            return;
        }
    }

    int fd = open(rp, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "District '%s' not found.\n", district); return; }

    Report r;
    int printed = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        int match = 1;
        for (int i = 0; i < cond_count; i++) {
            if (!match_condition(&r, fields[i], ops[i], values[i])) {
                match = 0; break;
            }
        }
        if (match) {
            char ts[32];
            struct tm *t = localtime(&r.timestamp);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
            printf("ID: %d | Inspector: %s | Lat: %.4f | Lon: %.4f | "
                   "Category: %s | Severity: %d | Time: %s | Desc: %s\n",
                   r.id, r.inspector, r.lat, r.lon,
                   r.category, r.severity, ts, r.description);
            printed++;
        }
    }
    close(fd);
    if (printed == 0) printf("No reports matched.\n");

    log_action(district, role, user, "filter");
}

static void check_symlinks(void) {
    DIR *d = opendir(".");
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "active_reports-", 15) != 0) continue;
        struct stat lst, st;
        lstat(ent->d_name, &lst);
        if (S_ISLNK(lst.st_mode)) {
            if (stat(ent->d_name, &st) < 0) {
                printf("WARNING: dangling symlink detected: %s\n", ent->d_name);
            }
        }
    }
    closedir(d);
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

    /* Parse arguments */
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
            /* collect remaining args as conditions */
            while (i+1 < argc && argv[i+1][0] != '-' && filter_count < 8) {
                filter_conds[filter_count++] = argv[++i];
            }
        }
    }

    if (!role || !user) {
        fprintf(stderr, "Usage: city_manager --role <manager|inspector> --user <name> "
                        "--<command> [args]\n");
        return 1;
    }
    if (!command) {
        fprintf(stderr, "No command specified.\n");
        return 1;
    }

    int is_manager = (strcmp(role, "manager") == 0);

    check_symlinks();

    if (strcmp(command, "add") == 0) {
        op_add(district, role, user, is_manager);
    } else if (strcmp(command, "list") == 0) {
        op_list(district, role, user);
    } else if (strcmp(command, "view") == 0) {
        op_view(district, report_id, role, user);
    } else if (strcmp(command, "remove_report") == 0) {
        op_remove_report(district, report_id, role, user, is_manager);
    } else if (strcmp(command, "update_threshold") == 0) {
        op_update_threshold(district, threshold, role, user, is_manager);
    } else if (strcmp(command, "filter") == 0) {
        if (filter_count == 0) {
            fprintf(stderr, "filter requires at least one condition.\n");
            return 1;
        }
        op_filter(district, filter_count, filter_conds, role, user);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return 1;
    }

    return 0;
}
