#include "kernel.h"

/* EL0 driver entry points */
__attribute__((section(".driver_text"), aligned(4096)))
void virtio_net_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_base;
    volatile u32 *ipc = (volatile u32 *)ipc_va;
    (void)task_id;
    ipc[0] = 1;  /* marker: started */
    u32 dev_id = mmio[2];
    ipc[0] = 2;  /* marker: read MMIO */
    ipc[1] = dev_id;
    __asm__ volatile(
        "mov x8, #4\n\t"
        "svc #0\n\t"
        : /* no outputs */
        : "r"(task_id)
    );
    ipc[0] = 3;  /* marker: about to return */
    for (;;) { /* unreachable */ }
}

__attribute__((section(".driver_text"), aligned(4096)))
void virtio_blk_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_base;
    volatile u32 *ipc = (volatile u32 *)ipc_va;
    u32 dev_id = mmio[2];
    ipc[0] = 1;
    (void)task_id;
    ipc[0] = 2;
    ipc[1] = dev_id;
    __asm__ volatile(
        "mov x8, #4\n\t"
        "svc #0\n\t"
        : /* no outputs */
        : "r"(task_id)
    );
    ipc[0] = 3;
    for (;;) { /* unreachable */ }
}

void virtio_net_driver(void) { (void)virtio_net_driver_entry; }
void virtio_blk_driver(void) { (void)virtio_blk_driver_entry; }
