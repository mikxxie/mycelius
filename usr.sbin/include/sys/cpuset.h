#ifndef _SYS_CPUSET_H_
#define _SYS_CPUSET_H_
#include <sched.h>

typedef cpu_set_t cpuset_t;

#define CPU_SETSIZE 1024
#define CPU_LEVEL_WHICH 0
#define CPU_WHICH_PID   0

static inline int cpuset_setaffinity(int level, int which, id_t id,
                                      size_t size, const cpuset_t *mask) {
    (void)level; (void)which;
    return sched_setaffinity(id, size, mask);
}

static inline int cpuset_getaffinity(int level, int which, id_t id,
                                      size_t size, cpuset_t *mask) {
    (void)level; (void)which;
    return sched_getaffinity(id, size, mask);
}

#endif
