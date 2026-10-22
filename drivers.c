#include "kernel.h"

/* EL0 driver entry points. Each runs at EL0 in its own task context with:
 *   x0 = MMIO base VA
 *   x1 = task id
 *   x2 = IPC page VA
 * The driver reads its MMIO device ID, writes it to the IPC page, then
 * returns to EL1 via SVC #4. The exit sequence is inlined: a call would
 * land in kernel .text, which the EL0 table does not map. */
#define DRIVER_EXIT() __asm__ volatile( \
    "mov x8, #4\n\t" \
    "svc #0\n\t" \
    ::: "x8", "memory")

__attribute__((section(".driver_text"), aligned(4096)))
void virtio_net_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_base;
    volatile u32 *ipc = (volatile u32 *)ipc_va;
    (void)task_id;
    ipc[0] = mmio[2]; /* virtio device ID at offset 0x008 */
    DRIVER_EXIT();
    for (;;) { /* unreachable */ }
}

__attribute__((section(".driver_text"), aligned(4096)))
void virtio_blk_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_base;
    volatile u32 *ipc = (volatile u32 *)ipc_va;
    (void)task_id;
    ipc[0] = mmio[2];
    DRIVER_EXIT();
    for (;;) { /* unreachable */ }
}

void virtio_net_driver(void) { (void)virtio_net_driver_entry; }
void virtio_blk_driver(void) { (void)virtio_blk_driver_entry; }
