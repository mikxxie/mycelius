#ifndef _ERRNO_H_
#define _ERRNO_H_

#include <sys/types.h>

extern int errno;

#define EAGAIN    35
#define EWOULDBLOCK EAGAIN
#define EINTR     4
#define EINVAL    22

#endif
