/* discoveryd - Mycelius device discovery daemon
 * Discovers nearby Bluetooth and WiFi devices
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void scan_bluetooth(void) {
    fprintf(stderr, "[discoveryd] Scanning Bluetooth...\n");
    /* Simulate scanning */
    usleep(500000);
    fprintf(stderr, "[discoveryd] Found: MyPhone (00:1A:7D:DA:71:11)\n");
    usleep(200000);
    fprintf(stderr, "[discoveryd] Found: MyHeadphones (00:11:22:33:44:55)\n");
}

static void scan_wifi(void) {
    fprintf(stderr, "[discoveryd] Scanning WiFi...\n");
    usleep(500000);
    fprintf(stderr, "[discoveryd] Found: MyHomeWiFi (CH:6, -45dBm)\n");
    usleep(200000);
    fprintf(stderr, "[discoveryd] Found: NeighborWiFi (CH:11, -72dBm)\n");
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    fprintf(stderr, "discoveryd: Mycelius device discovery daemon v1.0\n");

    while (g_running) {
        scan_bluetooth();
        usleep(2000000); /* 2 seconds */
        scan_wifi();
        usleep(2000000); /* 2 seconds */
    }

    fprintf(stderr, "discoveryd: shutting down\n");
    return 0;
}
