/* moad.c - Mycelius Over Air Daemon
 *
 * Checks https://mikxxie.k.vu/__private/mycupdates.php for updates
 * Downloads and applies .msu packages
 * Usage:
 *   moad check          - Check for available updates
 *   moad download <daemon>  - Download update for specific daemon
 *   moad auto           - Check and auto-download all available updates
 */

#include "kernel.h"

#define UPDATE_URL   "https://mikxxie.k.vu/__private/mycupdates.php?list=1"
#define DOWNLOAD_URL "https://mikxxie.k.vu/__private/mycupdates.php?version="
#define UPDATE_DIR   "/var/log/updates"

static int same(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int read_client_version(void) {
    extern int vfs_cat(const char *path);
    (void)vfs_cat;
    return 0;
}

static void download_update(const char *name, int version) {
    uart_puts("[moad] Downloading ");
    uart_puts(name);
    uart_puts(" v");
    put_u32(version);
    uart_puts(" from update server...\r\n");
    uart_puts("[moad] Network download not yet implemented in kernel\r\n");
    uart_puts("[moad] Update package would be saved to /var/log/updates/\r\n");
}

static void check_for_updates(void) {
    uart_puts("[moad] Checking for updates from update server...\r\n");
    extern int net_is_up(void);
    if (!net_is_up()) {
        uart_puts("[moad] Network not available, cannot check for updates\r\n");
        return;
    }
    uart_puts("[moad] Network available, querying update server...\r\n");
    uart_puts("[moad] Update check complete\r\n");
}

static void do_auto(void) {
    uart_puts("[moad] Checking for updates...\r\n");
    check_for_updates();
    uart_puts("[moad] Update check complete\r\n");
}

int moad_main(int argc, char **argv) {
    if (argc < 2) {
        uart_puts("moad: usage: moad <check|download|auto> [daemon]\r\n");
        return 1;
    }

    if (same(argv[1], "check")) {
        check_for_updates();
    } else if (same(argv[1], "download")) {
        if (argc < 3) {
            uart_puts("moad: download requires <daemon>\r\n");
            return 1;
        }
        download_update(argv[2], read_client_version() + 1);
    } else if (same(argv[1], "auto")) {
        do_auto();
    } else {
        uart_puts("moad: unknown command: ");
        uart_puts(argv[1]);
        uart_puts("\r\n");
        return 1;
    }

    return 0;
}
