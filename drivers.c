#include "kernel.h"
/* These are driver images' logic contracts.  They must ultimately execute at
 * EL0; the early bring-up calls them as stubs so hardware is never touched
 * from the kernel while EL0 page-table/context switching is being added. */
void virtio_net_driver(void) {
    /* EL0 only: negotiate virtio feature bits, own its 0x0a000000 MMIO mapping,
       then ipc_send(1, 0, EVENT_RX, descriptor_address) on an RX interrupt. */
    uart_puts("mycelius: virtio-net driver registered\r\n");
}
void virtio_blk_driver(void) {
    /* EL0 only: translate block IPC requests into virtio-blk descriptor chains. */
    uart_puts("mycelius: virtio-blk driver registered\r\n");
}
