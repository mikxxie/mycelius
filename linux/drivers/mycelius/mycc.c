/* mycc - Mycelius Commander shell
 * Built-in shell for mycelius init environment
 * Provides: ls, cat, echo, ps, kill, reboot, poweroff, help, ignite
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

static void cmd_ls(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    DIR *d = opendir(path);
    if (!d) { fprintf(stderr, "ls: cannot open '%s': %s\n", path, strerror(errno)); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        printf("%s  ", e->d_name);
    }
    printf("\n");
    closedir(d);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "cat: missing file operand\n"); return; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "cat: cannot open '%s': %s\n", argv[1], strerror(errno)); return; }
    int c;
    while ((c = fgetc(f)) != EOF) putchar(c);
    fclose(f);
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        fputs(argv[i], stdout);
        if (i < argc - 1) fputc(' ', stdout);
    }
    fputc('\n', stdout);
}

static void cmd_ps(int argc, char **argv) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { fprintf(stderr, "ps: cannot open /proc/stat\n"); return; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu ", 4) == 0 || strncmp(line, "intr ", 5) == 0 ||
            strncmp(line, "ctxt ", 5) == 0 || strncmp(line, "btime ", 6) == 0 ||
            strncmp(line, "processes ", 10) == 0) {
            fputs(line, stdout);
        }
    }
    fclose(f);
}

static void cmd_help(int argc, char **argv) {
    printf("mycc - Mycelius Commander Shell\n");
    printf("Built-in commands:\n");
    printf("  ls [dir]       - List directory contents\n");
    printf("  cat <file>     - Display file contents\n");
    printf("  echo [arg]     - Echo arguments\n");
    printf("  ps             - Show process status\n");
    printf("  kill <pid>     - Send signal to process\n");
    printf("  ignite <cmd>   - Run ignitectl command (start|stop|restart|status|list <daemon>)\n");
    printf("  discoveryd     - Run device discovery (wifi|bt)\n");
    printf("  moad           - Check for system updates\n");
    printf("  reboot         - Reboot the system\n");
    printf("  poweroff       - Power off the system\n");
    printf("  help           - Show this help\n");
    printf("  exit           - Exit the shell\n");
}

/* Backspace-aware line reader */
static int read_line(char *buf, int size) {
    int i = 0;
    while (i < size - 1) {
        int c = getchar();
        if (c == EOF) return -1;
        if (c == '\n') { buf[i] = 0; return i; }
        if (c == '\b' || c == 0x7f) {  /* Backspace / DEL */
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
                fflush(stdout);
            }
            continue;
        }
        if (c == '\r') { buf[i] = 0; return i; }
        buf[i++] = (char)c;
        putchar(c);
        fflush(stdout);
    }
    buf[i] = 0;
    return i;
}

int main(int argc, char **argv) {
    char line[256];
    char *args[16];

    printf("mycc - Mycelius Commander Shell v1.0\n");
    printf("Type 'help' for available commands.\n");

    while (1) {
        printf("mycc> ");
        fflush(stdout);

        if (read_line(line, sizeof(line)) < 0) break;

        /* Tokenize */
        int argc = 0;
        char *token = strtok(line, " \t");
        while (token && argc < 16) {
            args[argc++] = token;
            token = strtok(NULL, " \t");
        }
        args[argc] = NULL;

        if (argc == 0) continue;

        if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
            break;
        } else if (strcmp(args[0], "help") == 0) {
            cmd_help(argc, args);
        } else if (strcmp(args[0], "ls") == 0) {
            cmd_ls(argc, args);
        } else if (strcmp(args[0], "cat") == 0) {
            cmd_cat(argc, args);
        } else if (strcmp(args[0], "echo") == 0) {
            cmd_echo(argc, args);
        } else if (strcmp(args[0], "ps") == 0) {
            cmd_ps(argc, args);
        } else if (strcmp(args[0], "kill") == 0) {
            if (argc < 2) {
                fprintf(stderr, "kill: usage: kill <pid>\n");
            } else {
                kill(atoi(args[1]), SIGTERM);
            }
        } else if (strcmp(args[0], "reboot") == 0) {
            printf("mycelius: rebooting...\n");
        } else if (strcmp(args[0], "poweroff") == 0) {
            printf("mycelius: powering off...\n");
        } else if (strcmp(args[0], "ignite") == 0) {
            /* Delegate to ignitectl */
            if (argc < 2) {
                fprintf(stderr, "ignite: usage: ignite <start|stop|restart|status|list> <daemon>\n");
            } else {
                char *ign_argv[16];
                ign_argv[0] = "ignitectl";
                for (int i = 1; i < argc && i < 15; i++) {
                    ign_argv[i] = args[i];
                }
                ign_argv[argc < 15 ? argc : 15] = NULL;
                pid_t pid = fork();
                if (pid == 0) {
                    execv("/sbin/ignitectl", ign_argv);
                    _exit(127);
                }
                int status;
                waitpid(pid, &status, 0);
            }
        } else if (strcmp(args[0], "discoveryd") == 0) {
            /* Delegate to discoveryd */
            if (argc < 2) {
                fprintf(stderr, "discoveryd: usage: discoveryd wifi|bt [args]\n");
            } else {
                char *dis_argv[16];
                dis_argv[0] = "discoveryd";
                for (int i = 1; i < argc && i < 15; i++) {
                    dis_argv[i] = args[i];
                }
                dis_argv[argc < 15 ? argc : 15] = NULL;
                pid_t pid = fork();
                if (pid == 0) {
                    execv("/sbin/discoveryd", dis_argv);
                    _exit(127);
                }
                int status;
                waitpid(pid, &status, 0);
            }
        } else if (strcmp(args[0], "moad") == 0) {
            /* Delegate to updatesd (Moad - Mycelius Over Air Daemon) */
            if (argc < 2) {
                fprintf(stderr, "moad: usage: moad check|download <daemon>\n");
            } else {
                char *mo_argv[16];
                mo_argv[0] = "updatesd";
                for (int i = 1; i < argc && i < 15; i++) {
                    mo_argv[i] = args[i];
                }
                mo_argv[argc < 15 ? argc : 15] = NULL;
                pid_t pid = fork();
                if (pid == 0) {
                    execv("/sbin/updatesd", mo_argv);
                    _exit(127);
                }
                int status;
                waitpid(pid, &status, 0);
            }
        } else {
            fprintf(stderr, "mycc: unknown command: %s\n", args[0]);
        }
    }

    printf("mycc: goodbye\n");
    return 0;
}
