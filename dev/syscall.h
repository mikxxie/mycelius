#pragma once
#include <stdint.h>

/* Mycelius syscall numbers (passed in x8 / x8 register on AArch64).
 * These are the kernel ABI for EL0 user processes.
 */

/* Process control */
#define SYSCALL_EXIT      4   /* x0 = exit code */

/* Filesystem */
#define SYSCALL_WRITE     3   /* x0 = fd, x1 = buf, x2 = count */

/* IPC */
#define SYSCALL_IPC_SEND  5   /* x0 = from, x1 = to, x2 = type, x3 = value */
#define SYSCALL_IPC_RECV  6   /* x0 = to, x1 = &from, x2 = &type, x3 = &value */

/* Reserved for future use */
#define SYSCALL_MMAP      7
#define SYSCALL_MUNMAP    8
#define SYSCALL_FORK      9
#define SYSCALL_EXEC      10

#define SYSCALL_MAX       31
