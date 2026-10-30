#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef __FreeBSD__
#include <sys/cpuset.h>
#include <sys/rprio.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#endif

#include "lp_proto.h"
#include "render.h"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void set_high_priority(void) {
#ifdef __FreeBSD__
    struct rtprio rt;
    memset(&rt, 0, sizeof(rt));
    rt.type = RTP_PRIO_REALTIME;
    rt.prio = 50; /* UI shell above backboardd/input */
    if (rtprio(RTP_SET, 0, &rt) != 0) {
        fprintf(stderr, "launchpad: rtprio failed: %s\n", strerror(errno));
    }
    cpuset_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
                           sizeof(mask), &mask) != 0) {
        fprintf(stderr, "launchpad: cpuset failed: %s\n", strerror(errno));
    }
#else
    setpriority(PRIO_PROCESS, 0, -8);
#endif
}

static int connect_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ---------- scene graph ---------- */

#define MAX_TILES 32

struct tile {
    int32_t x, y, w, h;
    uint32_t color;
    uint32_t id;
    const char *label;
};

static struct tile g_tiles[MAX_TILES];
static int g_ntiles = 0;
static int g_fg_app = -1;       /* index into g_tiles, -1 = home */
static uint64_t g_last_touch = 0;

static void scene_init_home(int frame_w, int frame_h) {
    /* Android 4.4-style home grid: status bar + 4x4 app tiles. */
    g_ntiles = 0;
    int cols = 4, rows = 4;
    int pad = 14;
    int status_h = 44;
    int tile_w = (frame_w - pad * (cols + 1)) / cols;
    int tile_h = (frame_h - status_h - pad * (rows + 1)) / rows;
    if (tile_w < 1) tile_w = 1;
    if (tile_h < 1) tile_h = 1;

    const uint32_t colors[] = {
        0x004caf50, 0x002196f3, 0x009c27b0, 0x00ff9800,
        0x00f44336, 0x00009688, 0x00795548, 0x00607d8b,
        0x00ffeb3b, 0x009e9e9e, 0x008bc34a, 0x0003a9f4,
        0x00e91e63, 0x00ffc107, 0x0000bcd4, 0x007b1fa2,
    };
    const char *labels[] = {
        "Phone","Mail","Safari","Music",
        "Maps","Photos","Camera","Clock",
        "Settings","App Store","iBooks","News",
        "Weather","Stocks","Notes","Reminders",
    };
    int n = 0;
    for (int r = 0; r < rows && n < MAX_TILES; r++) {
        for (int c = 0; c < cols && n < MAX_TILES; c++) {
            struct tile *t = &g_tiles[n++];
            t->x = pad + c * (tile_w + pad);
            t->y = status_h + pad + r * (tile_h + pad);
            t->w = tile_w;
            t->h = tile_h;
            t->color = colors[n % (sizeof(colors)/sizeof(colors[0]))];
            t->id = (uint32_t)n;
            t->label = labels[n % (sizeof(labels)/sizeof(labels[0]))];
        }
    }
    g_ntiles = n;
}

static int hit_tile(int x, int y) {
    for (int i = 0; i < g_ntiles; i++) {
        struct tile *t = &g_tiles[i];
        if (x >= t->x && x < t->x + t->w &&
            y >= t->y && y < t->y + t->h) {
            return i;
        }
    }
    return -1;
}

/* ---------- compose ---------- */

static void compose(struct render *r, int frame_w, int frame_h) {
    /* Background: dark charcoal (like iOS home). */
    render_clear(r, 0x001a1a1a);

    /* Status bar. */
    render_rect(r, 0, 0, frame_w, 44, 0x00000000);
    render_rect(r, 0, 0, frame_w, 2, 0x00333333);

    /* Draw home tiles. */
    for (int i = 0; i < g_ntiles; i++) {
        struct tile *t = &g_tiles[i];
        uint32_t color = t->color;
        if (i == g_fg_app) {
            /* Highlight selected tile. */
            color = 0x00555555;
        }
        render_rect(r, t->x, t->y, t->w, t->h, color);
        /* Tile border. */
        render_rect(r, t->x, t->y, t->w, 2, 0x00ffffff);
        render_rect(r, t->x, t->y, 2, t->h, 0x00ffffff);
    }

    /* Foreground app: fullscreen overlay when launched. */
    if (g_fg_app >= 0 && g_fg_app < g_ntiles) {
        struct tile *t = &g_tiles[g_fg_app];
        render_rect(r, 0, 44, frame_w, frame_h - 44, t->color);
        /* App title bar (no window chrome - iOS style). */
        render_rect(r, 0, 44, frame_w, 40, 0x00000000);
        render_rect(r, 0, 44, frame_w, 2, 0x00222222);
        /* A "home" indicator. */
        render_rect(r, frame_w / 2 - 24, 86, 48, 8, 0x00ffffff);
    }
}

static void forward_touch_to_app(int app_id, const struct bb_event *ev) {
    /* v0: LaunchPad dictates the app; it forwards the touch to the
     * foreground app's Mach-port-equivalent (Unix socket on FreeBSD).
     * The app is blind to other windows; it only receives its own events. */
    fprintf(stderr,
            "launchpad: touch type=%u app=%u x=%d y=%d (forwarded to app)\n",
            ev->type, (uint32_t)app_id, ev->x, ev->y);

    /* Wire-format stub: build an LP_MSG_TOUCH_FORWARD for the app socket. */
    (void)app_id;
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    const char *bb_sock = LP_BACKBOARD_SOCK;
    const char *out_ppm = "/tmp/launchpad_frame.ppm";
    int frame_w = LP_FRAME_W;
    int frame_h = LP_FRAME_H;

    int c;
    while ((c = getopt(argc, argv, "b:o:w:h:")) != -1) {
        switch (c) {
        case 'b': bb_sock = optarg; break;
        case 'o': out_ppm = optarg; break;
        case 'w': frame_w = atoi(optarg); break;
        case 'h': frame_h = atoi(optarg); break;
        default:
            fprintf(stderr,
                    "usage: launchpad [-b sock] [-o ppm] [-w W] [-h H]\n");
            return 1;
        }
    }

    if (frame_w <= 0 || frame_h <= 0) {
        fprintf(stderr, "launchpad: invalid frame size\n");
        return 1;
    }

    set_high_priority();

    int bb_fd = connect_unix(bb_sock);
    if (bb_fd < 0) {
        fprintf(stderr, "launchpad: cannot connect to %s: %s\n",
                bb_sock, strerror(errno));
        return 1;
    }
    fprintf(stderr, "launchpad: connected to backboardd\n");

    struct render r;
    if (render_init(&r, frame_w, frame_h) != 0) {
        fprintf(stderr, "launchpad: render init failed\n");
        close(bb_fd);
        return 1;
    }

    scene_init_home(frame_w, frame_h);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &(struct sigaction){ .sa_handler = SIG_IGN }, NULL);

    uint64_t last_frame = 0;

    fprintf(stderr, "launchpad: rendering %dx%d @ %dms vsync\n",
            frame_w, frame_h, LP_VSYNC_MS);

    while (g_running) {
        struct pollfd pfd;
        pfd.fd = bb_fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, LP_VSYNC_MS);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Drain all pending input events. */
        if (pfd.revents & POLLIN) {
            struct bb_event bev;
            while (recv(bb_fd, &bev, sizeof(bev), MSG_DONTWAIT) ==
                   (ssize_t)sizeof(bev)) {
                if (bev.type == BB_EV_TOUCH_DOWN ||
                    bev.type == BB_EV_TOUCH_UP) {
                    int idx = hit_tile(bev.x, bev.y);
                    if (bev.type == BB_EV_TOUCH_DOWN && idx >= 0) {
                        g_fg_app = idx;
                        g_last_touch = bev.timestamp_ns;
                        forward_touch_to_app((uint32_t)idx, &bev);
                        fprintf(stderr,
                                "launchpad: launched app %u (%s)\n",
                                g_tiles[idx].id, g_tiles[idx].label);
                    } else if (bev.type == BB_EV_TOUCH_DOWN) {
                        /* Tap outside any tile = go home. */
                        if (g_fg_app >= 0) {
                            g_fg_app = -1;
                            fprintf(stderr, "launchpad: home\n");
                        }
                    }
                }
            }
        }

        /* Vsync render cadence. */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        if (last_frame == 0 || now - last_frame >=
            (uint64_t)LP_VSYNC_MS * 1000000ULL) {
            last_frame = now;
            compose(&r, frame_w, frame_h);
            if (render_write_ppm(&r, out_ppm) != 0) {
                fprintf(stderr, "launchpad: cannot write %s\n", out_ppm);
            }
        }
    }

    render_fini(&r);
    close(bb_fd);
    fprintf(stderr, "launchpad: exiting\n");
    return 0;
}
