#ifndef _SYS_RPRIO_H_
#define _SYS_RPRIO_H_
#include <sys/types.h>
#define RTP_PRIO_REALTIME 2
#define RTP_PRIO_NORMAL   3
#define RTP_PRIO_IDLE     4
#define RTP_SET           1
#define RTP_GET           0
struct rtprio { unsigned short type; unsigned short prio; };
int rtprio(int, pid_t, struct rtprio *);
#endif
