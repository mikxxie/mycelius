/* updatesd - Mycelius System Update Daemon
 *
 * Periodically checks https://mikxxie.k.vu/__private/mycupdates.php
 * for new .msu packages and downloads them to /var/log/updates/.
 * Runs as a background daemon, checking every 30 seconds.
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
#define CHECK_INTERVAL 30  /* seconds */

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
    }
}

static void download_update(const char *name, int version) {
    char url[512];
    char dest[512];
    char cmd[1024];

    snprintf(url, sizeof(url), "%s%d", DOWNLOAD_URL, version);
    snprintf(dest, sizeof(dest), "%s/%s", UPDATE_DIR, name);

    /* Use curl to download (available in initramfs) */
    snprintf(cmd, sizeof(cmd),
             "curl -sf -o '%s' '%s' && echo 'updatesd: downloaded %s v%d'",
             dest, url, name, version);
    system(cmd);
}

static void check_for_updates(void) {
    /* Fetch list of available updates */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -sf '%s' 2>/dev/null", UPDATE_URL);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    pclose(fp);

    /* Very simple JSON parsing: look for "daemonname":N patterns */
    /* Format: {"server_version":N,"daemons":{"name1":v1,"name2":v2}} */
    char *p = buf;
    while ((p = strstr(p, "\"daemons\"")) != NULL) {
        p = strchr(p, '{');
        if (!p) break;
        p++; /* skip { */

        while (*p && *p != '}') {
            /* Skip whitespace and commas */
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
            if (*p == '}') break;

            /* Read daemon name (quoted string) */
            if (*p != '"') { p++; continue; }
            p++; /* skip opening quote */
            char name[64];
            int ni = 0;
            while (*p && *p != '"' && ni < (int)sizeof(name) - 1) {
                name[ni++] = *p++;
            }
            name[ni] = '\0';

            /* Skip to colon */
            while (*p && *p != ':') p++;
            if (!*p) break;
            p++; /* skip colon */

            /* Skip whitespace */
            while (*p == ' ') p++;

            /* Read version number */
            int ver = 0;
            while (*p >= '0' && *p <= '9') {
                ver = ver * 10 + (*p - '0');
                p++;
            }

            if (ni > 0 && ver > 0) {
                printf("updatesd: %s server v%d\n", name, ver);

                /* Compare with client version */
                int client_ver = read_client_version();
                if (ver > client_ver) {
                    printf("updatesd: %s update available: v%d (client: v%d)\n",
                           name, ver, client_ver);
                    download_update(name, ver);
                }
            }
        }
    }
}

int main(void) {
    /* Ensure update directory exists */
    mkdir(UPDATE_DIR, 0755);

    int client_version = read_client_version();
    printf("updatesd: started (client version: %d)\n", client_version);

    while (1) {
        check_for_updates();
        sleep(CHECK_INTERVAL);
    }

    return 0;
}
