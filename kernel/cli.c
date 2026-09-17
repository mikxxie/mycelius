#include "kernel.h"

/* Bounded serial console; it consumes neither heap nor user process memory. */
static char line[48];
static u32 used;
static int same(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void put_u32(u32 n) {
    char digits[11]; u32 i = 0;
    if (!n) { uart_puts("0"); return; }
    while (n) { digits[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) { char out[2] = { digits[--i], 0 }; uart_puts(out); }
}
static void prompt(void) { uart_puts("mycelius> "); }
static const char *arg(const char *command, u32 skip) { while (skip--) ++command; return *command == ' ' ? command + 1 : 0; }
static void execute(void) {
    if (same(line, "help")) uart_puts("help ps mem run cli restart net|blk pwd ls [DIR] cd DIR mkdir DIR net status|up|down virtio blk read0 ext4 probe|ls\r\n");
    else if (same(line, "ps")) for (u32 i = 1; i < MAX_TASKS; ++i) { const struct task *t = task_get(i);
        if (t && t->state != TASK_EMPTY) { uart_puts("pid "); put_u32(i); uart_puts(t->state == TASK_READY ? " ready\r\n" : " faulted\r\n"); }
    } else if (same(line, "mem")) { put_u32(page_free_count()); uart_puts(" free 4KiB pages\r\n"); }
    else if (same(line, "run cli")) user_cli_start();
    else if (same(line, "pwd")) vfs_pwd();
    else if (same(line, "ls")) vfs_ls();
    else if (line[0] == 'l' && line[1] == 's' && line[2] == ' ') { if (vfs_ls_path(arg(line, 2))) uart_puts("ls: no such directory\r\n"); }
    else if (line[0] == 'c' && line[1] == 'd' && line[2] == ' ') { if (vfs_cd(arg(line, 2))) uart_puts("cd: no such directory\r\n"); }
    else if (line[0] == 'm' && line[1] == 'k' && line[2] == 'd' && line[3] == 'i' && line[4] == 'r' && line[5] == ' ') {
        int r = vfs_mkdir(arg(line, 5));
        if (r == -2) uart_puts("mkdir: filesystem is read-only; run make format-disk\r\n");
        else if (r) uart_puts("mkdir: cannot create directory\r\n");
    }
    else if (same(line, "net status")) net_status();
    else if (same(line, "net up")) net_set_up(1);
    else if (same(line, "net down")) net_set_up(0);
    else if (same(line, "virtio")) virtio_status();
    else if (same(line, "blk read0")) uart_puts(virtio_blk_read0() ? "blk: read failed\r\n" : "blk: sector 0 read complete\r\n");
    else if (same(line, "ext4 probe")) uart_puts(ext4_probe() ? "ext4: no valid superblock\r\n" : "ext4: valid superblock found\r\n");
    else if (same(line, "ext4 ls")) { if (ext4_ls_root()) uart_puts("ext4: unsupported or unreadable root\r\n"); }
    else if (same(line, "restart net")) task_restart(1);
    else if (same(line, "restart blk")) task_restart(2);
    else if (line[0]) uart_puts("unknown command\r\n");
}
void cli_poll(void) {
    int c = uart_getc_nonblock(); if (c < 0) return;
    if (c == '\r' || c == '\n') { uart_puts("\r\n"); line[used] = 0; execute(); used = 0; prompt(); return; }
    if ((c == 8 || c == 127) && used) { --used; uart_puts("\b \b"); return; }
    if (c >= 32 && c <= 126 && used + 1 < sizeof line) { line[used++] = (char)c; char out[2] = { (char)c, 0 }; uart_puts(out); }
}
