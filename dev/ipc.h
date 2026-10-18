#pragma once
#include <stdint.h>
#include <stddef.h>

/* Mycelius development headers - public syscall/ABI surface.
 * These headers describe the kernel<->userland ABI. They are stable
 * across kernel revisions unless otherwise noted.
 */

/* IPC message types */
enum mycelius_msg_type {
    MYCELIUS_MSG_NONE = 0,
    MYCELIUS_MSG_ARP_REPLY = 1,   /* net driver -> kernel: ARP reply received */
    MYCELIUS_MSG_BLK_REQUEST = 2, /* driver -> kernel: block I/O request */
    MYCELIUS_MSG_NET_RX = 3,      /* net driver -> kernel: Ethernet frame received */
    MYCELIUS_MSG_MAX = 255,
};

struct mycelius_message {
    uint32_t type;      /* message type (MYCELIUS_MSG_*) */
    uint32_t reserved;  /* padding / reserved */
    uint64_t value;     /* payload / pointer / value */
};

/* IPC syscall numbers (x0 = syscall number on SYSCALL instruction) */
#define SYSCALL_IPC_SEND   5
#define SYSCALL_IPC_RECV   6
#define SYSCALL_EXIT       4
#define SYSCALL_WRITE      3

/* Send an IPC message to another task. Returns 0 on success, -1 on failure.
 * from: caller task id, to: destination task id, type: message type,
 * value: message payload. */
int mycelius_send(uint32_t destination, const struct mycelius_message *message);

/* Receive next IPC message. Blocks until a message is available.
 * to: our task id, from/out_type/value_out: output pointers (may be NULL).
 * Returns 0 on message received, -1 if none available (should not block). */
int mycelius_recv(uint32_t to, uint32_t *from, uint32_t *out_type,
                  uint64_t *value_out);

/* Exit the current EL0 task with status code. Never returns. */
void mycelius_exit(int code);

/* Write a string to the serial console (diagnostic). */
void mycelius_puts(const char *s);
