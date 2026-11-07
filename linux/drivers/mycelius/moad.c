/* moad - Mycelius Over Air Daemon
 * Checks https://mikxxie.k.vu/__private/mycupdates.php for updates
 * Downloads and applies .msu packages
 * Usage:
 *   moad check          - Check for available updates
 *   moad download <daemon>  - Download update for specific daemon
 *   moad auto           - Check and auto-download all available updates
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define UPDATE_URL   "https://mikxxie.k.vu/__private/mycupdates.php?list=1"
#define DOWNLOAD_URL "https://mikxxie.k.vu/__private/mycupdates.php?version="
#define UPDATE_DIR   "/var/log/updates"
#define CLIENT_FILE  "/var/log/updates/client_version"

static int read_client_version(void) {
    char path[256];
    snprintf(path, sizeof(path), "%s/client_version", UPDATE_DIR);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int v = 0;
    fscanf(f, "%d", &v);
    fclose(f);
    return v;
}

static void write_client_version(int v) {
    char path[256];
    snprintf(path, sizeof(path), "%s/client_version", UPDATE_DIR);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d", v);
        fclose(f);
        printf("moad: client version updated to %d\n", v);
    }
}

static void download_update(const char *name, int version) {
    char url[512];
    char dest[512];
    char cmd[1024];

    snprintf(url, sizeof(url), "%s%d", DOWNLOAD_URL, version);
    snprintf(dest, sizeof(dest), "%s/%s", UPDATE_DIR, name);

    snprintf(cmd, sizeof(cmd),
             "curl -sf -o '%s' '%s' && echo 'moad: downloaded %s v%d to %s'",
             dest, url, name, version, dest);
    system(cmd);
}

static void check_for_updates(void) {
    /* Fetch list of available updates */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -sf '%s' 2>/dev/null", UPDATE_URL);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "moad: cannot reach update server\n");
        return;
    }

    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    pclose(fp);

    /* Simple JSON parsing for daemon versions */
    char *p = buf;
    while ((p = strstr(p, "\"daemons\"")) != NULL) {
        p = strchr(p, '{');
        if (!p) break;
        p++;

        while (*p && *p != '}') {
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
            if (*p == '}') break;

            if (*p != '"') { p++; continue; }
            p++;
            char name[64];
            int ni = 0;
            while (*p && *p != '"' && ni < (int)sizeof(name) - 1) {
                name[ni++] = *p++;
            }
            name[ni] = '\0';

            while (*p && *p != ':') p++;
            if (!*p) break;
            p++;
            while (*p == ' ') p++;

            int ver = 0;
            while (*p >= '0' && *p <= '9') {
                ver = ver * 10 + (*p - '0');
                p++;
            }

            if (ni > 0 && ver > 0) {
                int client_ver = read_client_version();
                printf("moad: %s server v%d (client v%d)\n", name, ver, client_ver);
                if (ver > client_ver) {
                    printf("moad: %s update available: v%d -> downloading...\n", name, ver);
                    download_update(name, ver);
                    write_client_version(ver);
                }
            }
        }
    }
}

static void do_auto(void) {
    printf("moad: checking for updates...\n");
    mkdir(UPDATE_DIR, 0755);
    check_for_updates();
    printf("moad: update check complete\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: moad <check|download|auto> [daemon]\n");
        return 1;
    }

    if (strcmp(argv[1], "check") == 0) {
        mkdir(UPDATE_DIR, 0755);
        check_for_updates();
    } else if (strcmp(argv[1], "download") == 0) {
        if (argc < 3) {
            fprintf(stderr, "moad: download requires <daemon>\n");
            return 1;
        }
        mkdir(UPDATE_DIR, 0755);
        download_update(argv[2], read_client_version() + 1);
    } else if (strcmp(argv[1], "auto") == 0) {
        do_auto();
    } else {
        fprintf(stderr, "moad: unknown command: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
