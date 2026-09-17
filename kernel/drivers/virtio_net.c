#include "virtio.h"

/* EL0 virtio-net queue, Ethernet, ARP, IPv4 and ICMP wait until ext4
 * read/write is verified across a QEMU reboot. This file only claims the
 * device so the rest of the kernel has a stable driver symbol. */
void virtio_net_driver(void) {
    uart_puts("mycelius: virtio-net driver registered\r\n");
}
