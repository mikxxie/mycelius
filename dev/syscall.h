#pragma once
#include <sys/types.h>

/* Mycelius syscall numbers - AArch64 ABI: passed in x8 register on AArch64.
 * These mirror the BSD syscall numbering philosophy: small, stable,
 * and grouped by subsystem.
 */

/* Process control */
#define SYS_exit          1   /* x0 = exit code */
#define SYSCALL_EXIT      SYSCALL_EXIT

/* File descriptors */
#define SYS_open          2   /* x0 = path, x1 = flags, x2 = mode */
#define SYS_close         3   /* x0 = fd */
#define SYS_read          0   /* x0 = fd, x1 = buf, x2 = count */
#define SYS_write         1   /* x0 = fd, x1 = buf, x2 = count */
#define SYS_lseek         9   /* x0 = fd, x1 = offset, x2 = whence */
#define SYS_fstat         10  /* x0 = fd, x1 = &stat */

/* Process control */
#define SYS_fork          2   /* returns pid to parent, 0 to child */
#define SYS_execve        11  /* x0 = path, x1 = argv, x2 = envp */
#define SYS_wait4         3   /* x0 = pid, x1 = &status, x2 = options, x3 = rusage */
#define SYS_kill          4   /* x0 = pid, x1 = sig */
#define SYS_getpid        5   /* x0 = nothing */
#define SYS_getppid       6   /* x0 = nothing */

/* IPC */
#define SYS_ipc_send      7   /* x0 = from, x1 = to, x2 = type, x3 = value */
#define SYS_ipc_recv      8   /* x0 = to, x1 = &from, x2 = &type, x3 = &value */

/* Memory */
#define SYS_mmap          9   /* x0 = addr, x1 = len, x2 = prot, x3 = flags, x4 = fd, x5 = offset */
#define SYS_munmap        10  /* x0 = addr, x1 = len */
#define SYS_mprotect      11  /* x0 = addr, x1 = len, x2 = prot */
#define SYS_mmap_anon     12  /* x0 = addr, x1 = len, x2 = prot, x3 = flags */

/* File system */
#define SYS_openat        13  /* x0 = dirfd, x1 = path, x2 = flags, x3 = mode */
#define SYS_close         3   /* alias above */
#define SYS_read          0   /* alias above */
#define SYS_write         1   /* alias above */
#define SYS_lseek         9   /* alias above */
#define SYS_fstat         10  /* alias above */
#define SYS_unlink        14  /* x0 = path */
#define SYS_mkdir         15  /* x0 = path, x1 = mode */
#define SYS_rmdir         16  /* x0 = path */
#define SYS_chdir         17  /* x0 = path */
#define SYS_getcwd        18  /* x0 = buf, x1 = size */

/* Network */
#define SYS_socket        19  /* x0 = domain, x1 = type, x3 = protocol */
#define SYS_bind          20  /* x0 = fd, x1 = addr, x2 = addrlen */
#define SYS_connect       21  /* x0 = fd, x1 = addr, x2 = addrlen */
#define SYS_listen        22  /* x0 = fd, x1 = backlog */
#define SYS_accept        23  /* x0 = fd, x1 = addr, x2 = addrlen */
#define SYS_recv          24  /* x0 = fd, x1 = buf, x2 = len, x3 = flags */
#define SYS_send          25  /* x0 = fd, x1 = buf, x2 = len, x3 = flags */

#define SYSCALL_MAX       63

/* Flag bits */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0008
#define O_TRUNC    0x0010
#define O_APPEND   0x0020
#define O_NONBLOCK 0x0004

/* Socket types */
#define SOCK_STREAM  1
#define SOCK_DGRAM   2

/* Address families */
#define AF_INET      2

/* Protocols */
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

/* mmap flags */
#define MAP_SHARED   0x01
#define MAP_PRIVATE  0x02
#define MAP_ANON     0x10
#define MAP_FIXED    0x20

/* mprotect prot */
#define PROT_NONE    0
#define PROT_READ    1
#define PROT_WRITE   2
#define PROT_EXEC    4

/* errno values */
#define EINVAL   1
#define ENOENT   2
#define EACCES   3
#define ENOMEM   4
#define EBADF    5
#define EISDIR   6
#define ENOTDIR  7
#define EEXIST   8
#define ENOSPC   9
#define EIO      10
#define EINTR    11
#define ETIMEDOUT 12

#endif /* _SYS_SYSCALL_H_ */
