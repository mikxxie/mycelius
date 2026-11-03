#ifndef _SYS_RPRIO_H_
#define _SYS_RPRIO_H_
#include <sched.h>

#define RTP_PRIO_REALTIME SCHED_FIFO
#define RTP_PRIO_NORMAL   SCHED_OTHER
#define RTP_PRIO_IDLE     SCHED_IDLE
#define RTP_SET           0
#define RTP_GET           1

struct rtprio {
    int type;
    int prio;
};

static inline int rtprio(int which, pid_t pid, struct rtprio *rtp) {
    struct sched_param sp;
    int policy;
    int ret;

    if (which == RTP_GET) {
        ret = sched_getscheduler(pid);
        if (ret < 0) return -1;
        policy = ret;
        if (policy == SCHED_FIFO) rtp->type = RTP_PRIO_REALTIME;
        else if (policy == SCHED_RR) rtp->type = RTP_PRIO_REALTIME;
        else if (policy == SCHED_IDLE) rtp->type = RTP_PRIO_IDLE;
        else rtp->type = RTP_PRIO_NORMAL;
        ret = sched_getparam(pid, &sp);
        if (ret < 0) return -1;
        rtp->prio = sp.sched_priority;
        return 0;
    }

    if (rtp->type == RTP_PRIO_REALTIME) policy = SCHED_FIFO;
    else if (rtp->type == RTP_PRIO_IDLE) policy = SCHED_IDLE;
    else policy = SCHED_OTHER;

    sp.sched_priority = rtp->prio;
    return sched_setscheduler(pid, policy, &sp);
}

#endif
