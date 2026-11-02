#ifndef _SYS_CPUSET_H_
#define _SYS_CPUSET_H_
#include <sys/types.h>
#define CPU_SETSIZE 256
typedef struct { unsigned long __bits[CPU_SETSIZE/64]; } cpuset_t;
#define CPU_ZERO(s)      __builtin_memset((s),0,sizeof(*(s)))
#define CPU_SET(n,s)     ((s)->__bits[(n)/64] |= (1UL<<((n)%64)))
#define CPU_CLR(n,s)     ((s)->__bits[(n)/64] &= ~(1UL<<((n)%64)))
#define CPU_ISSET(n,s)   (!!((s)->__bits[(n)/64] & (1UL<<((n)%64))))
#define CPU_LEVEL_WHICH  3
#define CPU_WHICH_PID    2
int cpuset_setaffinity(int,int,id_t,size_t,const cpuset_t *);
int cpuset_getaffinity(int,int,id_t,size_t,cpuset_t *);
#endif
