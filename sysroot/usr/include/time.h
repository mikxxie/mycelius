#ifndef _TIME_H_
#define _TIME_H_

#include <sys/types.h>

struct timespec {
    long tv_sec;
    long tv_nsec;
};

int clock_gettime(int clock_id, struct timespec *tp);

#define CLOCK_MONOTONIC 4

#endif
