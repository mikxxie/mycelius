#ifndef _SYS_SOCKET_H_
#define _SYS_SOCKET_H_

#include <sys/types.h>

#define AF_UNIX    1
#define AF_LOCAL   AF_UNIX
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define MSG_NOSIGNAL  0x4000
#define MSG_DONTWAIT  0x40

typedef uint32_t socklen_t;

struct sockaddr {
    uint8_t sa_len;
    uint8_t sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    uint8_t sun_len;
    uint8_t sun_family;
    char sun_path[104];
};

int socket(int domain, int type, int protocol);
int bind(int s, const struct sockaddr *addr, socklen_t namelen);
int listen(int s, int backlog);
int accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int connect(int s, const struct sockaddr *addr, socklen_t namelen);
ssize_t send(int s, const void *buf, size_t len, int flags);
ssize_t recv(int s, void *buf, size_t len, int flags);

#endif
