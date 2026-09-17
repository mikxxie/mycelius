#include "kernel.h"
/* Control plane only until virtio-net's queue/interrupt driver exists. */
static int interface_up;
void net_set_up(int up) { interface_up = up != 0; uart_puts(interface_up ? "net0: administratively up\r\n" : "net0: administratively down\r\n"); }
void net_status(void) {
    uart_puts("net0: ");
    uart_puts(interface_up ? "UP" : "DOWN");
    uart_puts(" (virtio-net transport not attached)\r\n");
}
