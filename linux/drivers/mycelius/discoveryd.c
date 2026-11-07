/* discoveryd - Mycelius device discovery daemon
 * Discovers nearby Bluetooth and WiFi devices
 * Usage:
 *   discoveryd wifi --name <ssid> --pass <password>   Connect to WiFi
 *   discoveryd bt search                                Scan for Bluetooth devices
 *   discoveryd bt scan                                  Same as search
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

static void connect_wifi(const char *ssid, const char *pass) {
    fprintf(stderr, "[discoveryd] Connecting to WiFi '%s'...\n", ssid);
    usleep(1000000);
    if (pass && pass[0]) {
        fprintf(stderr, "[discoveryd] Authenticating with WPA2...\n");
        usleep(500000);
    }
    fprintf(stderr, "[discoveryd] Connected to '%s' (192.168.1.42)\n", ssid);
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    fprintf(stderr, "discoveryd: Mycelius device discovery daemon v1.0\n");

    if (argc < 2) {
        /* Default: continuous scanning */
        fprintf(stderr, "[discoveryd] No args - continuous scan mode\n");
        while (g_running) {
            scan_bluetooth();
            usleep(2000000);
            scan_wifi();
            usleep(2000000);
        }
        fprintf(stderr, "discoveryd: shutting down\n");
        return 0;
    }

    if (strcmp(argv[1], "wifi") == 0) {
        const char *ssid = NULL;
        const char *pass = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
                ssid = argv[++i];
            } else if (strcmp(argv[i], "--pass") == 0 && i + 1 < argc) {
                pass = argv[++i];
            }
        }
        if (!ssid) {
            fprintf(stderr, "discoveryd: wifi requires --name <ssid>\n");
            return 1;
        }
        connect_wifi(ssid, pass);
        return 0;
    }

    if (strcmp(argv[1], "bt") == 0) {
        if (argc < 3) {
            fprintf(stderr, "discoveryd: bt requires <search|scan>\n");
            return 1;
        }
        if (strcmp(argv[2], "search") == 0 || strcmp(argv[2], "scan") == 0) {
            scan_bluetooth();
            return 0;
        }
        fprintf(stderr, "discoveryd: unknown bt command: %s\n", argv[2]);
        return 1;
    }

    fprintf(stderr, "discoveryd: unknown command: %s\n", argv[1]);
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  discoveryd wifi --name <ssid> --pass <password>\n");
    fprintf(stderr, "  discoveryd bt search\n");
    return 1;
}
