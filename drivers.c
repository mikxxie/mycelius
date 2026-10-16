#include "kernel.h"

__attribute__((section(".driver_text"), aligned(4096)))
void virtio_net_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_base;
    volatile u32 *ipc = (volatile u32 *)ipc_va;
    u32 dev_id = mmio[2];
    ipc[0] = dev_id;
    uart_puts("mycelius: net driver: wrote IPC, calling SVC #5\r\n");
    __asm__ volatile(
        "mov x8, #5\n\t"
        "svc #0\n\t"
        : /* no outputs */
        : "r"(task_id), "r"(0), "r"(1), "r"(dev_id)
    );
    uart_puts("mycelius: net driver: returning\r\n");
}

__attribute__((section(".driver_text"), aligned(4096)))
void virtio_blk_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_base;
    volatile u32 *ipc = (volatile u32 *)ipc_va;
    u32 dev_id = mmio[2];
    uart_puts("mycelius: blk driver: reading MAC\r\n");
    ipc[0] = dev_id;
    (void)task_id;
    uart_puts("mycelius: blk driver: wrote IPC, returning\r\n");
}

void virtio_net_driver(void) { (void)virtio_net_driver_entry; }
void virtio_blk_driver(void) { (void)virtio_blk_driver_entry; }
