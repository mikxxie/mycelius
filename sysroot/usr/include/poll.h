#ifndef _POLL_H_
#define _POLL_H_

#include <sys/types.h>

#define POLLIN    0x0001
#define POLLOUT   0x0004

struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif
