/* mycelius init — PID 1, BSD-style daemon supervisor for QEMU aarch64 virt
 *
 * Boots with a "Loading.." spinner, then starts daemons and supervises them,
 * restarting any that crash — just like a phone OS.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static volatile sig_atomic_t g_shutdown = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_shutdown = 1;
}

static void spinner_tick(int tick) {
    const char *frames = "/-\\|";
    fprintf(stderr, "\rLoading mycelius... %c", frames[tick % 4]);
}

static pid_t start_daemon(const char *name, char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "mycelius: failed to exec %s: %s\n", name, strerror(errno));
        _exit(127);
    }
    if (pid < 0) {
        fprintf(stderr, "mycelius: failed to fork %s: %s\n", name, strerror(errno));
        return -1;
    }
    fprintf(stderr, "mycelius: started %s (pid %d)\n", name, pid);
    return pid;
}

static void mount_filesystems(void) {
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
    mkdir("/var/run", 0755);
    mkdir("/var/log", 0755);
    chmod("/var/run", 0755);
    chmod("/var/log", 0755);

    /* Create device nodes for console, null, zero, and framebuffer */
    mknod("/dev/fb0", 0666 | S_IFCHR, makedev(29, 0));
    mknod("/dev/tty", 0666 | S_IFCHR, makedev(5, 0));
    mknod("/dev/console", 0666 | S_IFCHR, makedev(5, 1));
    mknod("/dev/null", 0666 | S_IFCHR, makedev(1, 3));
    mknod("/dev/zero", 0666 | S_IFCHR, makedev(1, 5));
    mkdir("/dev/dri", 0755);
    /* /dev/fb0 is created by DRM + devtmpfs - mknod creates the node early,
       DRM driver binds to it when ready */
}

static void show_spinner(int seconds) {
    fprintf(stderr, "\r");
    for (int i = 0; i < seconds * 4; i++) {
        spinner_tick(i);
        usleep(250000);
    }
    fprintf(stderr, "\rLoading mycelius... done.\n");
}

static int wait_for_socket(const char *path, int timeout_ms) {
    for (int i = 0; i < timeout_ms / 100; i++) {
        if (access(path, F_OK) == 0) return 0;
        usleep(100000);
    }
    return -1;
}

static int wait_for_device(const char *path, int timeout_ms) {
    int fd;
    for (int i = 0; i < timeout_ms / 100; i++) {
        fd = open(path, O_RDWR);
        if (fd >= 0) {
            close(fd);
            return 0;
        }
        usleep(100000);
    }
    return -1;
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_IGN); /* reap children automatically */

    mount_filesystems();

    /* Show loading spinner on console (like a phone bootloader) */
    show_spinner(2);

    /* Start backboardd first */
    char *bb_argv[] = {"/sbin/backboardd", "-s", "/var/run/lp_bb.sock", NULL};
    pid_t bb_pid = start_daemon("backboardd", bb_argv);
    if (bb_pid > 0) {
        /* Wait for backboardd socket before starting launchpad */
        if (wait_for_socket("/var/run/lp_bb.sock", 5000) == 0) {
            fprintf(stderr, "mycelius: backboardd socket ready\n");
        } else {
            fprintf(stderr, "mycelius: backboardd socket timeout\n");
        }
    }

    /* Start launchpad after backboardd AND framebuffer are ready */
    char *lp_argv[] = {"/sbin/launchpad", "-b", "/var/run/lp_bb.sock",
                       "-o", "/var/run/lp_frame.ppm", NULL};
    /* Wait for /dev/fb0 to become usable (DRM + fbdev emulation may take time) */
    if (access("/dev/fb0", F_OK) != 0) {
        fprintf(stderr, "mycelius: waiting for /dev/fb0...\n");
        if (wait_for_device("/dev/fb0", 30000) == 0) {
            fprintf(stderr, "mycelius: /dev/fb0 ready\n");
        } else {
            fprintf(stderr, "mycelius: /dev/fb0 timeout, launchpad will use PPM fallback\n");
        }
    } else {
        fprintf(stderr, "mycelius: /dev/fb0 present, waiting for DRM...\n");
        if (wait_for_device("/dev/fb0", 30000) == 0) {
            fprintf(stderr, "mycelius: /dev/fb0 ready\n");
        } else {
            fprintf(stderr, "mycelius: /dev/fb0 timeout, launchpad will use PPM fallback\n");
        }
    }
    pid_t lp_pid = start_daemon("launchpad", lp_argv);
    if (lp_pid > 0) {
        fprintf(stderr, "mycelius: UI stack running\n");
    }

    /* Start mycc shell after launchpad */
    char *cc_argv[] = {"/sbin/mycc", NULL};
    pid_t cc_pid = start_daemon("mycc", cc_argv);
    if (cc_pid > 0) {
        fprintf(stderr, "mycelius: mycc shell started (pid %d)\n", cc_pid);
    }

    /* Start moad (Mycelius Over Air Daemon) - checks for updates */
    char *moad_argv[] = {"/sbin/moad", "auto", NULL};
    pid_t moad_pid = start_daemon("moad", moad_argv);
    if (moad_pid > 0) {
        fprintf(stderr, "mycelius: moad started (pid %d)\n", moad_pid);
    }

    /* Supervision loop: restart crashed daemons */
    while (!g_shutdown) {
        sleep(1);
        /* Check backboardd */
        if (bb_pid > 0) {
            int status;
            pid_t r = waitpid(bb_pid, &status, WNOHANG);
            if (r == bb_pid) {
                fprintf(stderr, "mycelius: backboardd exited, restarting...\n");
                bb_pid = start_daemon("backboardd", bb_argv);
                if (bb_pid > 0) {
                    wait_for_socket("/var/run/lp_bb.sock", 5000);
                }
            }
        }
        /* Check launchpad */
        if (lp_pid > 0) {
            int status;
            pid_t r = waitpid(lp_pid, &status, WNOHANG);
            if (r == lp_pid) {
                fprintf(stderr, "mycelius: launchpad exited, restarting...\n");
                lp_pid = start_daemon("launchpad", lp_argv);
            }
        }
        /* Check moad */
        if (moad_pid > 0) {
            int status;
            pid_t r = waitpid(moad_pid, &status, WNOHANG);
            if (r == moad_pid) {
                fprintf(stderr, "mycelius: moad exited, restarting...\n");
                moad_pid = start_daemon("moad", moad_argv);
            }
        }
    }

    /* Shutdown: kill all daemons */
    if (bb_pid > 0) kill(bb_pid, SIGTERM);
    if (lp_pid > 0) kill(lp_pid, SIGTERM);
    if (moad_pid > 0) kill(moad_pid, SIGTERM);
}
