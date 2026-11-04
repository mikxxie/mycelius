/* mycelius init — PID 1, starts the UI stack on boot */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

int main(void) {
    /* Mount essential filesystems */
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mkdir("/var/run", 0755);
    mkdir("/var/log", 0755);

    fprintf(stderr, "mycelius: init starting UI stack\n");

    /* Start backboardd */
    char *bb_argv[] = {"/sbin/backboardd", "-s", "/var/run/lp_bb.sock", NULL};
    pid_t bb_pid = fork();
    if (bb_pid == 0) {
        execv("/sbin/backboardd", bb_argv);
        fprintf(stderr, "mycelius: failed to exec backboardd\n");
        _exit(127);
    }
    fprintf(stderr, "mycelius: started backboardd (pid %d)\n", bb_pid);

    sleep(1);

    /* Start launchpad */
    char *lp_argv[] = {"/sbin/launchpad", "-b", "/var/run/lp_bb.sock",
                       "-o", "/var/run/lp_frame.ppm", NULL};
    pid_t lp_pid = fork();
    if (lp_pid == 0) {
        execv("/sbin/launchpad", lp_argv);
        fprintf(stderr, "mycelius: failed to exec launchpad\n");
        _exit(127);
    }
    fprintf(stderr, "mycelius: started launchpad (pid %d)\n", lp_pid);

    fprintf(stderr, "mycelius: UI stack running\n");

    /* Keep init alive */
    while (1) sleep(3600);

    return 0;
}
