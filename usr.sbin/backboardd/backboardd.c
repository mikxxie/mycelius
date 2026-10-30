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
#include <sys/stat.h>

#ifdef __FreeBSD__
#include <sys/cpuset.h>
#include <sys/rprio.h>
#include <sys/resource.h>
#include <dev/evdev/input.h>
#else
#include <linux/input.h>
#include <sys/resource.h>
#endif

#include "lp_proto.h"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void set_high_priority(void) {
#ifdef __FreeBSD__
    /* Real-time, high priority: input must never stall. */
    struct rtprio rt;
    memset(&rt, 0, sizeof(rt));
    rt.type = RTP_PRIO_REALTIME;
    rt.prio = 40;
    if (rtprio(RTP_SET, 0, &rt) != 0) {
        fprintf(stderr, "backboardd: rtprio failed: %s\n", strerror(errno));
    }
    /* Bind to core 0 (shared with LaunchPad foreground on big.LITTLE). */
    cpuset_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
                           sizeof(mask), &mask) != 0) {
        fprintf(stderr, "backboardd: cpuset failed: %s\n", strerror(errno));
    }
#else
    /* Linux CI: best-effort nice boost. */
    setpriority(PRIO_PROCESS, 0, -10);
#endif
}

/* Open an evdev input device. Returns -1 on failure. */
static int open_evdev(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "backboardd: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(stderr, "backboardd: opened input %s\n", path);
    return fd;
}

/* Read one evdev input_event into out. Returns 0 on success, -1 none. */
static int read_evdev_event(int fd, struct bb_event *out) {
#ifdef __FreeBSD__
    struct input_event ev;
#else
    struct input_event ev;
#endif
    ssize_t n = read(fd, &ev, sizeof(ev));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        return -1;
    }
    if (n < (ssize_t)sizeof(ev)) return -1;

    /* Normalise evdev codes into our protocol. */
    memset(out, 0, sizeof(*out));
    out->timestamp_ns = (uint64_t)ev.time.tv_sec * 1000000000ULL +
                        (uint64_t)ev.time.tv_usec * 1000ULL;

    if (ev.type == EV_KEY) {
        out->type = BB_EV_KEY;
        out->x = ev.code;   /* key code in x */
        out->y = ev.value;  /* 0 up, 1 down, 2 repeat in y */
        return 0;
    }
    if (ev.type == EV_ABS) {
        /* Treat ABS_MT_POSITION_X/Y as touch. v0: single-touch via X. */
        out->type = (ev.value == 0) ? BB_EV_TOUCH_UP : BB_EV_TOUCH_DOWN;
        out->x = ev.code == ABS_X ? ev.value : out->x;
        out->y = ev.code == ABS_Y ? ev.value : out->y;
        return 0;
    }
    return -1; /* ignore other event types */
}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int make_unix_server(const char *path) {
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    chmod(path, 0666);
    if (listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    const char *evdev_path = NULL;
    const char *sock_path = LP_BACKBOARD_SOCK;
    int synth = 0;

    int c;
    while ((c = getopt(argc, argv, "d:s:hv")) != -1) {
        switch (c) {
        case 'd': evdev_path = optarg; break;
        case 's': sock_path = optarg; break;
        case 'v': /* verbosity handled via stderr always */ break;
        case 'h':
        default:
            fprintf(stderr,
                    "usage: backboardd [-d evdev] [-s sock] [-h]\n");
            return c == 'h' ? 0 : 1;
        }
    }

    /* Synth mode: generate synthetic touch events on a timer for CI. */
    if (!evdev_path) synth = 1;

    set_high_priority();

    int evfd = -1;
    if (!synth && evdev_path) {
        evfd = open_evdev(evdev_path);
        if (evfd < 0) return 1;
    }

    int listen_fd = make_unix_server(sock_path);
    if (listen_fd < 0) {
        fprintf(stderr, "backboardd: cannot bind %s: %s\n",
                sock_path, strerror(errno));
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &(struct sigaction){ .sa_handler = SIG_IGN }, NULL);

    int client_fd = -1;
    uint64_t synth_next = 0;

    fprintf(stderr, "backboardd: listening on %s\n", sock_path);

    while (g_running) {
        struct pollfd pfd[2];
        int nfd = 0;
        pfd[nfd].fd = listen_fd;
        pfd[nfd].events = POLLIN;
        nfd++;
        if (client_fd >= 0) {
            pfd[nfd].fd = client_fd;
            pfd[nfd].events = POLLIN;
            nfd++;
        }

        int timeout = synth ? LP_VSYNC_MS : 50;
        int pr = poll(pfd, nfd, timeout);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Accept a new LaunchPad connection. */
        if (pfd[0].revents & POLLIN) {
            int cfd = accept(listen_fd, NULL, NULL);
            if (cfd >= 0) {
                if (client_fd >= 0) close(client_fd);
                client_fd = cfd;
                fprintf(stderr, "backboardd: launchpad connected\n");
            }
        }

        /* Forward messages from launchpad (e.g. ack) - v0 we only send. */
        if (client_fd >= 0 && (pfd[1].revents & POLLIN)) {
            char buf[64];
            ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                close(client_fd);
                client_fd = -1;
                fprintf(stderr, "backboardd: launchpad disconnected\n");
            }
        }

        /* Read input events and forward. */
        if (evfd >= 0) {
            struct bb_event bev;
            while (read_evdev_event(evfd, &bev) == 0) {
                if (client_fd >= 0) {
                    send_all(client_fd, &bev, sizeof(bev));
                }
            }
        }

        /* Synthetic event stream for CI (no hardware). */
        if (synth && client_fd >= 0) {
            uint64_t now = 0;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
            if (now >= synth_next) {
                struct bb_event bev;
                memset(&bev, 0, sizeof(bev));
                static int sx = 100;
                bev.type = BB_EV_TOUCH_DOWN;
                bev.x = sx % (LP_FRAME_W - 40) + 20;
                bev.y = 400;
                bev.timestamp_ns = now;
                send_all(client_fd, &bev, sizeof(bev));
                sx += 80;
                synth_next = now + (uint64_t)LP_VSYNC_MS * 1000000ULL;
            }
        }
    }

    if (client_fd >= 0) close(client_fd);
    close(listen_fd);
    unlink(sock_path);
    if (evfd >= 0) close(evfd);
    fprintf(stderr, "backboardd: exiting\n");
    return 0;
}
