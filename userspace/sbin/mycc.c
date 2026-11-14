/* mycc.c - Mycelius Commander Shell
 *
 * EL0 userspace shell daemon. Provides an interactive command-line interface
 * for the mycelius kernel. Built as a static aarch64 EL0 binary and launched
 * by the kernel as a user task.
 */

#include "kernel.h"

/* Forward declarations */
static void cmd_help(void);
static void cmd_ls(void);
static void cmd_ps(void);
static void cmd_mem(void);
static void cmd_cat(const char *path);
static void parse_and_execute(char *line);
extern void put_u32(u32 n);

/* ------------------------------------------------------------------ */
/* Tiny string helpers                                                */
/* ------------------------------------------------------------------ */
static int same(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* ------------------------------------------------------------------ */
/* Tiny line reader with backspace support                            */
/* ------------------------------------------------------------------ */
static int read_line(char *buf, int size) {
    int i = 0;
    while (i < size - 1) {
        int c = uart_getc_nonblock();
        if (c < 0) {
            if (i == 0) {
                /* Wait for first character */
                for (volatile int j = 0; j < 50000; ++j) __asm__ volatile("nop");
                c = uart_getc_nonblock();
                if (c < 0) continue;
            } else {
                return i;
            }
        }
        if (c == '\r' || c == '\n') {
            buf[i] = 0;
            uart_puts("\r\n");
            return i;
        }
        if ((c == 8 || c == 0x7f) && i > 0) {
            i--;
            uart_puts("\b \b");
            continue;
        }
        if (c >= 32 && c <= 126) {
            buf[i++] = (char)c;
            char out[2] = { (char)c, 0 };
            uart_puts(out);
        }
    }
    buf[i] = 0;
    return i;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */
static void cmd_help(void) {
    uart_puts("mycc - Mycelius Commander Shell v1.0\r\n");
    uart_puts("Built-in commands:\r\n");
    uart_puts("  help            - Show this help\r\n");
    uart_puts("  ls              - List root directory\r\n");
    uart_puts("  cat <file>      - Print file contents\r\n");
    uart_puts("  ps              - Show task status\r\n");
    uart_puts("  mem             - Show free memory\r\n");
    uart_puts("  discoveryd wifi --name <ssid> --pass <pass>\r\n");
    uart_puts("  discoveryd bt search\r\n");
    uart_puts("  moad check      - Check for OTA updates\r\n");
    uart_puts("  moad download <daemon>\r\n");
    uart_puts("  ignite <start|stop|restart|status|list> <daemon>\r\n");
    uart_puts("  gui             - Render GUI framebuffer demo\r\n");
    uart_puts("  exit            - Exit shell\r\n");
}

static void cmd_ls(void) {
    uart_puts("bin/\r\n");
    uart_puts("dev/\r\n");
    uart_puts("etc/\r\n");
    uart_puts("sbin/\r\n");
    uart_puts("tmp/\r\n");
    uart_puts("usr/\r\n");
}

static void cmd_cat(const char *path) {
    if (!path || !*path) {
        uart_puts("cat: missing file operand\r\n");
        return;
    }
    extern int vfs_cat(const char *path);
    if (vfs_cat(path)) uart_puts("cat: cannot read file\r\n");
}

static void cmd_ps(void) {
    uart_puts("PID  NAME        STATE\r\n");
    uart_puts(" 1   backboardd   ready\r\n");
    uart_puts(" 2   launchpad    ready\r\n");
    uart_puts(" 3   mycc         ready\r\n");
    uart_puts(" 4   discoveryd   ready\r\n");
    uart_puts(" 5   moad         ready\r\n");
}

static void cmd_mem(void) {
    extern u32 page_free_count(void);
    put_u32(page_free_count());
    uart_puts(" free 4KiB pages\r\n");
}


/* Parse and execute a command line */
static void parse_and_execute(char *line) {
    char *args[16];
    int argc = 0;
    char *tok = line;
    while (*tok == ' ') tok++;
    while (*tok && argc < 15) {
        args[argc++] = tok;
        while (*tok && *tok != ' ') tok++;
        if (*tok) { *tok = 0; tok++; }
        while (*tok == ' ') tok++;
    }
    args[argc] = 0;

    if (argc == 0) return;

    if (same(line, "help")) cmd_help();
    else if (same(line, "ls")) cmd_ls();
    else if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' && line[3] == ' ') cmd_cat(line + 4);
    else if (same(line, "ps")) cmd_ps();
    else if (same(line, "mem")) cmd_mem();
    else if (same(line, "gui")) {
        uart_puts("gui: rendering framebuffer demo...\r\n");
        extern void gui_demo(void);
        extern int virtio_gpu_display(void);
        gui_demo();
        virtio_gpu_display();
        uart_puts("gui: done\r\n");
    }
    else if (line[0] == 'm' && line[1] == 'o' && line[2] == 'a' && line[3] == 'd' && (line[4] == ' ' || line[4] == 0)) {
        extern int moad_main(int argc, char **argv);
        moad_main(argc, args);
    }
    else if (line[0] == 'd' && line[1] == 'i' && line[2] == 's' && line[3] == 'c' && line[4] == 'o' && line[5] == 'v' && line[6] == 'e' && line[7] == 'r' && line[8] == 'y' && line[9] == 'd' && (line[10] == ' ' || line[10] == 0)) {
        extern int discoveryd_main(int argc, char **argv);
        discoveryd_main(argc, args);
    }
    else if (line[0] == 'i' && line[1] == 'g' && line[2] == 'n' && line[3] == 'i' && line[4] == 't' && line[5] == 'e' && line[6] == 0) {
        uart_puts("ignite: usage: ignite <start|stop|restart|status|list> <daemon>\r\n");
    }
    else if (line[0]) {
        uart_puts("mycc: unknown command: ");
        uart_puts(line);
        uart_puts("\r\n");
    }
}

int mycc_main(void) {
    char line[256];
    uart_puts("mycc - Mycelius Commander Shell v1.0\r\n");
    uart_puts("Type 'help' for available commands.\r\n");

    while (1) {
        uart_puts("mycc> ");
        int n = read_line(line, sizeof(line));
        if (n < 0) break;
        if (n == 0) continue;
        parse_and_execute(line);
    }

    uart_puts("mycc: goodbye\r\n");
    return 0;
}
