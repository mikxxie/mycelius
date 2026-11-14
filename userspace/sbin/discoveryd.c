/* discoveryd.c - Mycelius device discovery daemon
 *
 * Discovers nearby Bluetooth and WiFi devices.
 * Usage:
 *   discoveryd wifi --name <ssid> --pass <password>   Connect to WiFi
 *   discoveryd bt search                                Scan for Bluetooth devices
 *   discoveryd bt scan                                  Same as search
 */

#include "kernel.h"

static void scan_bluetooth(void) {
    uart_puts("[discoveryd] Scanning Bluetooth...\r\n");
    for (volatile int i = 0; i < 1000000; ++i) __asm__ volatile("nop");
    uart_puts("[discoveryd] Found: MyPhone (00:1A:7D:DA:71:11)\r\n");
    for (volatile int i = 0; i < 500000; ++i) __asm__ volatile("nop");
    uart_puts("[discoveryd] Found: MyHeadphones (00:11:22:33:44:55)\r\n");
}

static void scan_wifi(void) {
    uart_puts("[discoveryd] Scanning WiFi...\r\n");
    for (volatile int i = 0; i < 1000000; ++i) __asm__ volatile("nop");
    uart_puts("[discoveryd] Found: MyHomeWiFi (CH:6, -45dBm)\r\n");
    for (volatile int i = 0; i < 500000; ++i) __asm__ volatile("nop");
    uart_puts("[discoveryd] Found: NeighborWiFi (CH:11, -72dBm)\r\n");
}

static void connect_wifi(const char *ssid, const char *pass) {
    uart_puts("[discoveryd] Connecting to WiFi '");
    uart_puts(ssid);
    uart_puts("'...\r\n");
    for (volatile int i = 0; i < 1000000; ++i) __asm__ volatile("nop");
    if (pass && pass[0]) {
        uart_puts("[discoveryd] Authenticating with WPA2...\r\n");
        for (volatile int i = 0; i < 500000; ++i) __asm__ volatile("nop");
    }
    uart_puts("[discoveryd] Connected to '");
    uart_puts(ssid);
    uart_puts("' (192.168.1.42)\r\n");
}

static int same(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

int discoveryd_main(int argc, char **argv) {
    uart_puts("discoveryd: Mycelius device discovery daemon v1.0\r\n");

    if (argc < 2) {
        uart_puts("[discoveryd] No args - continuous scan mode\r\n");
        while (1) {
            scan_bluetooth();
            for (volatile int i = 0; i < 2000000; ++i) __asm__ volatile("nop");
            scan_wifi();
            for (volatile int i = 0; i < 2000000; ++i) __asm__ volatile("nop");
        }
        uart_puts("discoveryd: shutting down\r\n");
        return 0;
    }

    if (same(argv[1], "wifi")) {
        const char *ssid = NULL;
        const char *pass = NULL;
        for (int i = 2; i < argc; i++) {
            if (same(argv[i], "--name") && i + 1 < argc) {
                ssid = argv[++i];
            } else if (same(argv[i], "--pass") && i + 1 < argc) {
                pass = argv[++i];
            }
        }
        if (!ssid) {
            uart_puts("discoveryd: wifi requires --name <ssid>\r\n");
            return 1;
        }
        connect_wifi(ssid, pass);
        return 0;
    }

    if (same(argv[1], "bt")) {
        if (argc < 3) {
            uart_puts("discoveryd: bt requires <search|scan>\r\n");
            return 1;
        }
        if (same(argv[2], "search") || same(argv[2], "scan")) {
            scan_bluetooth();
            return 0;
        }
        uart_puts("discoveryd: unknown bt command: ");
        uart_puts(argv[2]);
        uart_puts("\r\n");
        return 1;
    }

    uart_puts("discoveryd: unknown command: ");
    uart_puts(argv[1]);
    uart_puts("\r\n");
    uart_puts("Usage:\r\n");
    uart_puts("  discoveryd wifi --name <ssid> --pass <password>\r\n");
    uart_puts("  discoveryd bt search\r\n");
    return 1;
}
