#include "kernel.h"

/* Bounded serial console; it consumes neither heap nor user process memory. */
static char line[128];
static u32 used;
static int same(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static u32 mnu_prompt; /* 0=mycelius>, 1=> */
static int mnu_unsaved; /* 1 if MNU has unsaved changes */
static u32 mnu_filename; /* filename length, or 0 */
static char mnu_save_name[32]; /* persisted filename for save message */

/* Utility functions */
static void put_u32(u32 n) {
    char digits[11]; u32 i = 0;
    if (!n) { uart_puts("0"); return; }
    while (n) { digits[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) { char out[2] = { digits[--i], 0 }; uart_puts(out); }
}

static const char *arg(const char *command, u32 skip) { while (skip--) ++command; return *command == ' ' ? command + 1 : 0; }

static void prompt(void); /* forward declaration */

/* Save current MNU file and mark as saved */
static void mnu_save(void) {
    if (mnu_filename) {
        uart_puts("[");
        uart_puts(mnu_save_name);
        uart_puts("] saved\r\n");
    } else {
        uart_puts("[New File] saved\r\n");
    }
    mnu_unsaved = 0;
}

/* Exit MNU, confirming discard if unsaved */
static void mnu_exit(void) {
    if (mnu_unsaved) {
        uart_puts("Save changes before exit? y/n\r\n");
    }
    mnu_prompt = 0;
    mnu_unsaved = 0;
    uart_puts("mycelius> ");
}

/* Handle MNU mode input */
static void mnu_handle_input(void) {
    uart_puts("Welcome to the MNU (Mycelius New Utility)\r\n");
    uart_puts("To save a file, press CTRL + S\r\n");
    uart_puts("To exit, press CTRL + X and press it again to confirm if unsaved.\r\n");
}

/* Display help */
static void help_disp(void) {
    uart_puts("help           - Show this help message\r\n"
        "ps             - Show task status\r\n"
        "mem            - Show free memory (4KiB pages)\r\n"
        "run cli        - Start user CLI\r\n"
        "pwd            - Show current working directory\r\n"
        "ls             - List directory contents\r\n"
        "ls <path>      - List directory at path\r\n"
        "cd <dir>       - Change directory\r\n"
        "mkdir <dir>    - Create directory\r\n"
        "rm [-r|--recursive] <path> - Remove file/directory\r\n"
        "net status     - Show network status\r\n"
        "net up         - Enable network\r\n"
        "net down       - Disable network\r\n"
        "virtio         - Show virtio device status\r\n"
        "blk read0      - Read sector 0 from disk\r\n"
        "ext4 probe     - Check ext4 filesystem validity\r\n"
        "ext4 ls        - List ext4 root directory\r\n"
        "restart net    - Restart virtio-net driver\r\n"
        "restart blk    - Restart virtio-blk driver\r\n"
        "mnu            - Show Mycelius New Utility menu\r\n");
}

/* Remove file or directory */
static void do_rm(const char *args) {
    if (!args) { uart_puts("usage: rm [-r|--recursive] PATH\r\n"); return; }
    while (*args == ' ') ++args;
    if (!*args) { uart_puts("usage: rm [-r|--recursive] PATH\r\n"); return; }
    int recursive = 0;
    const char *target = 0;
    while (*args) {
        while (*args == ' ') ++args;
        if (!*args) break;
        if (args[0] == '-') {
            if (args[1] == 'r' && (args[2] == ' ' || args[2] == 0)) {
                recursive = 1;
                args += 2;
            } else if (args[1] == 'R' && (args[2] == ' ' || args[2] == 0)) {
                recursive = 1;
                args += 2;
            } else if (args[1] == 'r' && args[2] == 'f' && (args[3] == ' ' || args[3] == 0)) {
                recursive = 1;
                args += 3;
            } else if (args[1] == '-' && args[2] == 'r' && args[3] == 'e' && args[4] == 'c' &&
                        args[5] == 'u' && args[6] == 'r' && args[7] == 's' && args[8] == 'i' &&
                        args[9] == 'v' && args[10] == 'e' && (args[11] == ' ' || args[11] == 0)) {
                recursive = 1;
                args += 11;
            } else {
                uart_puts("rm: unknown option\r\n");
                return;
            }
        } else {
            target = args;
            break;
        }
    }
    if (!target || !*target) {
        uart_puts("usage: rm [-r|--recursive] PATH\r\n");
        return;
    }
    int r = vfs_rm(target, recursive);
    if (r == -2) uart_puts("rm: cannot remove: is a directory (use -r)\r\n");
    else if (r) uart_puts("rm: cannot remove\r\n");
}

/* Main command execution */
static void execute(void) {
    if (same(line, "help")) help_disp();
    else if (same(line, "ps")) for (u32 i = 1; i < MAX_TASKS; ++i) { const struct task *t = task_get(i);
        if (t && t->state != TASK_EMPTY) { uart_puts("pid "); put_u32(i); uart_puts(t->state == TASK_READY ? " ready\r\n" : " faulted\r\n"); }
    } else if (same(line, "mem")) { put_u32(page_free_count()); uart_puts(" free 4KiB pages\r\n"); }
    else if (same(line, "run cli")) user_cli_start();
    else if (same(line, "pwd")) vfs_pwd();
    else if (same(line, "ls")) vfs_ls();
    else if (line[0] == 'l' && line[1] == 's' && line[2] == ' ') { if (vfs_ls_path(arg(line, 2))) uart_puts("ls: no such directory\r\n"); }
    else if (line[0] == 'c' && line[1] == 'd' && line[2] == ' ') { if (vfs_cd(arg(line, 2))) uart_puts("cd: no such directory\r\n"); }
    else if (line[0] == 'm' && line[1] == 'k' && line[2] == 'd' && line[3] == 'i' && line[4] == 'r' && line[5] == ' ') { if (vfs_mkdir(arg(line, 5))) uart_puts("mkdir: cannot create directory\r\n"); }
    else if (line[0] == 'r' && line[1] == 'm' && (line[2] == ' ' || line[2] == 0)) do_rm(arg(line, 2));
    else if (same(line, "net status")) net_status();
    else if (same(line, "net up")) net_set_up(1);
    else if (same(line, "net down")) net_set_up(0);
    else if (same(line, "virtio")) virtio_status();
    else if (same(line, "blk read0")) uart_puts(virtio_blk_read0() ? "blk: read failed\r\n" : "blk: sector 0 read complete\r\n");
    else if (same(line, "ext4 probe")) uart_puts(ext4_probe() ? "ext4: no valid superblock\r\n" : "ext4: valid superblock found\r\n");
    else if (same(line, "ext4 ls")) { if (ext4_ls_root()) uart_puts("ext4: unsupported or unreadable root\r\n"); }
    else if (same(line, "restart net")) task_restart(1);
    else if (same(line, "restart blk")) task_restart(2);
    else if (line[0] == 'm' && line[1] == 'n' && line[2] == 'u' && (line[3] == 0 || line[3] == ' ')) {
        mnu_prompt = 1;
        mnu_unsaved = 1;
        mnu_filename = 0;
        mnu_save_name[0] = 0;
        /* Check if a filename was provided */
        const char *a = arg(line, 3);
        if (a && *a) {
            u32 i = 0;
            while (a[i] && i < 31) { mnu_save_name[i] = a[i]; i++; }
            mnu_save_name[i] = 0;
            mnu_filename = i;
            /* Display filename */
            uart_puts("File: ");
            uart_puts(mnu_save_name);
            uart_puts("\r\n");
        }
        mnu_handle_input();
    }
    else if (mnu_prompt) {
        /* In MNU mode, only Ctrl+S and Ctrl+X are processed */
        if (used > 0) {
            /* Non-printable or other input - ignore */
        }
        /* The actual Ctrl+S/Ctrl+X handling is in cli_poll() */
    }
    else if (line[0]) uart_puts("unknown command\r\n");
}

/* Console poll */
void cli_poll(void) {
    int c = uart_getc_nonblock(); if (c < 0) return;
    if (c == '\r' || c == '\n') { uart_puts("\r\n"); line[used] = 0; execute(); used = 0; prompt(); return; }
    if ((c == 8 || c == 127) && used) { --used; uart_puts("\b \b"); return; }
    /* Handle Ctrl+S (19) and Ctrl+X (24) in MNU mode */
    if (mnu_prompt) {
        if (c == 19) { /* Ctrl+S */ 
            mnu_save();
            return;
        }
        if (c == 24) { /* Ctrl+X */
            mnu_exit();
            return;
        }
    }
    if (c >= 32 && c <= 126 && used + 1 < sizeof line) { line[used++] = (char)c; char out[2] = { (char)c, 0 }; uart_puts(out); }
}

/* Prompt */
static void prompt(void) {
    if (mnu_prompt) uart_puts("> ");
    else uart_puts("mycelius> ");
}

