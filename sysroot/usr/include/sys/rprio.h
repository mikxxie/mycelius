#ifndef _SYS_RPRIO_H_
#define _SYS_RPRIO_H_

#include <sys/types.h>

#define RTP_PRIO_NORMAL    0
#define RTP_PRIO_REALTIME  1
#define RTP_SET            1

struct rtprio {
    int type;
    int prio;
};

int rtprio(int func, pid_t pid, const struct rtprio *rtp);

#endif
