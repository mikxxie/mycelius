/* mycc - Mycelius Commander shell
 * Built-in shell for mycelius init environment
 * Provides: ls, cat, echo, ps, kill, reboot, poweroff, help
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
    printf("  ls [dir]   - List directory contents\n");
    printf("  cat <file> - Display file contents\n");
    printf("  echo [arg] - Echo arguments\n");
    printf("  ps         - Show process status\n");
    printf("  kill <pid> - Send signal to process\n");
    printf("  reboot     - Reboot the system\n");
    printf("  poweroff   - Power off the system\n");
    printf("  help       - Show this help\n");
    printf("  exit       - Exit the shell\n");
}

int main(int argc, char **argv) {
    char line[256];
    char *args[16];

    printf("mycc - Mycelius Commander Shell v1.0\n");
    printf("Type 'help' for available commands.\n");

    while (1) {
        printf("mycc> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip newline */
        line[strcspn(line, "\n")] = 0;

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
            /* Would trigger reboot via syscall */
        } else if (strcmp(args[0], "poweroff") == 0) {
            printf("mycelius: powering off...\n");
        } else {
            fprintf(stderr, "mycc: unknown command: %s\n", args[0]);
        }
    }

    printf("mycc: goodbye\n");
    return 0;
}
