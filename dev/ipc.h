#pragma once
#include <stdint.h>
#include <stddef.h>
#include <dev/syscall.h>

/* Mycelius IPC API - public header for EL0 user processes.
 * All IPC is synchronous and bounded: fixed-size mailboxes,
 * no heap allocation in kernel path.
 */

/* IPC message types */
enum mycelius_msg_type {
    MYCELIUS_MSG_NONE    = 0,
    MYCELIUS_MSG_ARP_REPLY = 1,   /* net driver -> kernel: ARP reply received */
    MYCELIUS_MSG_BLK_REQUEST = 2, /* driver -> kernel: block I/O request */
    MYCELIUS_MSG_NET_RX = 3,      /* net driver -> kernel: Ethernet frame received */
    MYCELIUS_MSG_MAX = 255,
};

/* IPC message - 24 bytes, fixed-size for bounded IPC mailboxes */
struct mycelius_message {
    u32 type;      /* message type (MYCELIUS_MSG_*) */
    u32 reserved;  /* padding / reserved */
    u64 value;     /* payload / pointer / value */
};

/* Send an IPC message to another task. Returns 0 on success, -1 on failure.
 * from: caller task id, to: destination task id, type: message type,
 * value: message payload. */
int mycelius_send(u32 from, u32 to, u32 type, u64 value);

/* Receive next IPC message. Blocks until a message is available.
 * to: our task id, from/out_type/value_out: output pointers (may be NULL).
 * Returns 0 on message received, -1 if none available (should not block). */
int mycelius_recv(u32 to, u32 *from, u32 *out_type, u64 *value_out);

/* Exit the current EL0 task with status code. Never returns. */
void mycelius_exit(int code);

/* Write a string to the serial console (diagnostic, kernel-only ABI). */
void mycelius_puts(const char *s);
