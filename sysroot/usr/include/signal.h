#ifndef _SIGNAL_H_
#define _SIGNAL_H_

#include <sys/types.h>

#define SIGINT    2
#define SIGTERM   15
#define SIGCHLD   20
#define SIGPIPE   13
#define SIG_IGN   ((void (*)(int))1)

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

struct sigaction {
    void (*sa_handler)(int);
    int sa_flags;
    void *sa_mask;
};

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
sighandler_t signal(int signum, sighandler_t handler);

#endif
