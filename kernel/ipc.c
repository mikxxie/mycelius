#include "kernel.h"
/* Fixed mailboxes are intentional: no unbounded kernel allocations or queues. */
struct message { u32 valid, from, type; u64 value; };
static struct message inbox[MAX_TASKS];
void ipc_init(void) { for (u32 i = 0; i < MAX_TASKS; ++i) inbox[i].valid = 0; }
int ipc_send(u32 from, u32 to, u32 type, u64 value) {
    if (from >= MAX_TASKS || to >= MAX_TASKS || inbox[to].valid) return -1;
    inbox[to] = (struct message){ 1, from, type, value }; return 0;
}
int ipc_recv(u32 to, u32 *from, u32 *type, u64 *value) {
    if (to >= MAX_TASKS || !inbox[to].valid) return -1;
    *from = inbox[to].from; *type = inbox[to].type; *value = inbox[to].value;
    inbox[to].valid = 0; return 0;
}
