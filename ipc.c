#include "kernel.h"

/* Fixed mailboxes are intentional: no unbounded kernel allocations or queues.
 * Messages are strictly validated: sender/recipient task IDs must be valid,
 * message type must be in range [1, MYCELIUS_MSG_MAX], and mailboxes are
 * single-slot bounded buffers. */
struct message { u32 valid, from, type; u64 value; };
static struct message inbox[MAX_TASKS];

void ipc_init(void) {
    for (u32 i = 0; i < MAX_TASKS; ++i) inbox[i].valid = 0;
}

int ipc_send(u32 from, u32 to, u32 type, u64 value) {
    /* Validate sender and destination IDs, and message type */
    if (from >= MAX_TASKS || to >= MAX_TASKS) return -1;
    if (!type || type > MYCELIUS_MSG_MAX) return -1;
    if (inbox[to].valid) return -1; /* mailbox busy */

    inbox[to] = (struct message){ 1, from, type, value };
    return 0;
}

int ipc_recv(u32 to, u32 *from, u32 *type, u64 *value) {
    if (to >= MAX_TASKS || !inbox[to].valid) return -1;
    if (from)  *from  = inbox[to].from;
    if (type)  *type  = inbox[to].type;
    if (value) *value = inbox[to].value;
    inbox[to].valid = 0;
    return 0;
}

int ipc_has_message(u32 to) {
    return (to < MAX_TASKS) ? inbox[to].valid : 0;
}
