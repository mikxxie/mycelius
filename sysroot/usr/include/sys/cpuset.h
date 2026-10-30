#ifndef _SYS_CPUSETP_H_
#define _SYS_CPUSETP_H_

#include <sys/types.h>

#define CPU_LEVEL_WHICH  1
#define CPU_WHICH_PID   -1

#define CPU_SETSIZE     1024

typedef struct {
    unsigned long bits[(CPU_SETSIZE + (8 * sizeof(unsigned long)) - 1) /
                       (8 * sizeof(unsigned long))];
} cpuset_t;

#define CPU_ZERO(cpusetp) \
    do { \
        for (int _i = 0; \
             _i < (int)((CPU_SETSIZE + (8 * sizeof(unsigned long)) - 1) / \
                        (8 * sizeof(unsigned long))); \
             _i++) \
            (cpusetp)->bits[_i] = 0UL; \
    } while (0)

#define CPU_SET(cpu, cpusetp) \
    do { \
        (cpusetp)->bits[(cpu) / (8 * sizeof(unsigned long))] |= \
            (1UL << ((cpu) % (8 * sizeof(unsigned long)))); \
    } while (0)

int cpuset_setaffinity(int which, int type, id_t id, size_t size,
                       const cpuset_t *mask);

#endif
