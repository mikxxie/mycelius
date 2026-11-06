/* ignitectl - Mycelius daemon controller
 * Controls daemons managed by init
 * Usage: ignitectl <start|stop|restart|status|list> <daemon>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static const char *DAEMON_DIR = "/sbin";
static const char *PID_DIR = "/var/run";

static int pid_exists(pid_t pid) {
    if (pid <= 0) return 0;
    return kill(pid, 0) == 0 || errno == ESRCH;
}

static pid_t read_pid(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pid", PID_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    pid_t pid;
    if (fscanf(f, "%d", &pid) != 1) { fclose(f); return -1; }
    fclose(f);
    return pid;
}

static void write_pid(const char *name, pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pid", PID_DIR, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%d\n", pid);
    fclose(f);
}

static void remove_pid(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pid", PID_DIR, name);
    unlink(path);
}

static void start_daemon(const char *name) {
    pid_t existing = read_pid(name);
    if (existing > 0 && pid_exists(existing)) {
        printf("ignitectl: %s already running (pid %d)\n", name, existing);
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", DAEMON_DIR, name);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child */
        setsid();
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
        execv(path, (char *[]){ (char *)name, NULL });
        _exit(127);
    }

    write_pid(name, pid);
    printf("ignitectl: started %s (pid %d)\n", name, pid);
}

static void stop_daemon(const char *name) {
    pid_t pid = read_pid(name);
    if (pid <= 0) {
        printf("ignitectl: %s not running\n", name);
        return;
    }

    if (kill(pid, SIGTERM) != 0) {
        fprintf(stderr, "ignitectl: failed to stop %s\n", name);
        return;
    }

    /* Wait up to 2 seconds for exit */
    for (int i = 0; i < 20; i++) {
        usleep(100000);
        if (kill(pid, 0) != 0 && errno == ESRCH) break;
    }

    remove_pid(name);
    printf("ignitectl: stopped %s\n", name);
}

static void restart_daemon(const char *name) {
    stop_daemon(name);
    usleep(200000); /* 200ms */
    start_daemon(name);
    printf("ignitectl: restarted %s\n", name);
}

static void status_daemon(const char *name) {
    pid_t pid = read_pid(name);
    if (pid > 0 && pid_exists(pid)) {
        printf("ignitectl: %s is running (pid %d)\n", name, pid);
    } else {
        printf("ignitectl: %s is not running\n", name);
    }
}

static void list_daemons(void) {
    const char *daemons[] = { "backboardd", "launchpad", NULL };
    for (int i = 0; daemons[i]; i++) {
        status_daemon(daemons[i]);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ignitectl <start|stop|restart|status|list> [daemon]\n");
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        list_daemons();
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "Usage: ignitectl %s <daemon>\n", cmd);
        return 1;
    }

    const char *daemon = argv[2];

    if (strcmp(cmd, "start") == 0) {
        start_daemon(daemon);
    } else if (strcmp(cmd, "stop") == 0) {
        stop_daemon(daemon);
    } else if (strcmp(cmd, "restart") == 0) {
        restart_daemon(daemon);
    } else if (strcmp(cmd, "status") == 0) {
        status_daemon(daemon);
    } else {
        fprintf(stderr, "ignitectl: unknown command: %s\n", cmd);
        return 1;
    }

    return 0;
}
