#include "kernel.h"

/* E1000 (Intel 82540EM) network fallback driver.
 * Based on FreeBSD /sys/dev/e1000/if_em.c but simplified for direct MMIO.
 * QEMU, VirtualBox, and VMware all emulate the 82540EM when using
 * -device e1000-82540em or -netdev e1000.
 *
 * Register offsets (from e1000_82540.c):
 *  CTRL     0x0000 - Control register
 *  STATUS   0x0008 - Device status
 *  RCTL     0x0100 - Receive control
 *  TCTL     0x0400 - Transmit control
 *  RDBAH/L  0x0280/0x0284 - Receive descriptor base address
 *  RDLEN    0x0288 - Receive descriptor length
 *  RDH/RDT  0x0290/0x0298 - Receive descriptor head/tail
 *  TDBAH/L  0x0380/0x0384 - Transmit descriptor base address
 *  TDLEN    0x0388 - Transmit descriptor length
 *  TDH/TDT  0x0390/0x0398 - Transmit descriptor head/tail
 *  ICR      0x00C0 - Interrupt cause read
 *  IMS      0x00D0 - Interrupt mask set
 */

#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_RDBAH     0x0288
#define E1000_RDBAL     0x0280
#define E1000_RDLEN     0x0288
#define E1000_RDH       0x0290
#define E1000_RDT       0x0298
#define E1000_TDBAH     0x0388
#define E1000_TDBAL     0x0384
#define E1000_TDLEN     0x0388
#define E1000_TDH       0x0390
#define E1000_TDT       0x0398
#define E1000_ICR       0x00C0
#define E1000_IMS       0x00D0

#define E1000_CTRL_RST  (1u << 0)   /* Software reset */
#define E1000_CTRL_SLU  (1u << 6)   /* Set link up */
#define E1000_CTRL_ASDE (1u << 5)   /* Auto-select detect */

#define E1000_RCTL_EN   (1u << 0)   /* Receiver enable */
#define E1000_RCTL_SBP  (1u << 1)   /* Store bad packets */
#define E1000_RCTL_UPE  (1u << 3)   /* Unicast promiscuous enable */
#define E1000_RCTL_MPE  (1u << 4)   /* Multicast promiscuous enable */
#define E1000_RCTL_LPE  (1u << 5)   /* Long packet enable */
#define E1000_RCTL_LBM_NONE (0u << 6)
#define E1000_RCTL_RDMTS_HALF (0u << 8)
#define E1000_RCTL_BAM  (1u << 15)  /* Broadcast accept mode */
#define E1000_RCTL_SZ_2048 (0u << 16)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TCTL_EN   (1u << 0)   /* Transmit enable */
#define E1000_TCTL_PSP  (1u << 3)   /* Pad short packets */
#define E1000_TCTL_CT   (0x0f << 4) /* Collision threshold */
#define E1000_TCTL_COLD (0x3f << 12) /* Collision distance */

#define E1000_DESC_DD   (1u << 0)   /* Descriptor done */
#define E1000_DESC_EOP  (1u << 1)   /* End of packet */

struct e1000_desc {
    u64 addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 special;
    u16 errors;
    u16 special2;
};

static volatile u32 *e1000_regs = (volatile u32 *)0xE0000000UL; /* PCI BAR0 */
static struct e1000_desc *e1000_rx_ring = (struct e1000_desc *)0x40100000UL;
static struct e1000_desc *e1000_tx_ring = (struct e1000_desc *)0x40110000UL;
static u16 e1000_rx_head = 0;
static u16 e1000_tx_head = 0;

#define E1000_RING_SIZE 8

void e1000_init(void) {
    u32 val;

    /* Software reset */
    e1000_regs[E1000_CTRL / 4] = E1000_CTRL_RST;
    for (volatile int i = 0; i < 10000; i++) {}
    e1000_regs[E1000_CTRL / 4] |= E1000_CTRL_ASDE | E1000_CTRL_SLU;

    /* Setup receive ring */
    e1000_regs[E1000_RDBAH / 4] = 0;
    e1000_regs[E1000_RDBAL / 4] = (u32)(u64)e1000_rx_ring;
    e1000_regs[E1000_RDLEN / 4] = E1000_RING_SIZE * sizeof(struct e1000_desc);
    e1000_regs[E1000_RDH / 4] = 0;
    e1000_regs[E1000_RDT / 4] = E1000_RING_SIZE - 1;

    /* Initialize receive descriptors */
    for (int i = 0; i < E1000_RING_SIZE; i++) {
        e1000_rx_ring[i].addr = 0x40120000UL + i * 2048; /* Packet buffers */
        e1000_rx_ring[i].length = 0;
        e1000_rx_ring[i].status = 0;
    }

    /* Setup transmit ring */
    e1000_regs[E1000_TDBAH / 4] = 0;
    e1000_regs[E1000_TDBAL / 4] = (u32)(u64)e1000_tx_ring;
    e1000_regs[E1000_TDLEN / 4] = E1000_RING_SIZE * sizeof(struct e1000_desc);
    e1000_regs[E1000_TDH / 4] = 0;
    e1000_regs[E1000_TDT / 4] = 0;

    /* Enable receiver */
    e1000_regs[E1000_RCTL / 4] = E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                                E1000_RCTL_BAM | E1000_RCTL_SECRC |
                                E1000_RCTL_LPE | E1000_RCTL_SZ_2048;

    /* Enable transmitter */
    e1000_regs[E1000_TCTL / 4] = E1000_TCTL_EN | E1000_TCTL_PSP |
                                E1000_TCTL_CT | E1000_TCTL_COLD;

    e1000_regs[E1000_IMS / 4] = 0xFFFFFFFF; /* Enable all interrupts */

    uart_puts("e1000: initialized at 0xE0000000\r\n");
}

void e1000_poll(void) {
    u32 status = e1000_regs[E1000_STATUS / 4];
    u32 icr = e1000_regs[E1000_ICR / 4]; /* Read and clear interrupts */

    if (icr & (1u << 0)) { /* RX interrupt */
        while (!(e1000_rx_ring[e1000_rx_head].status & E1000_DESC_DD)) {
            /* Packet available */
            u16 length = e1000_rx_ring[e1000_rx_head].length;
            u8 *pkt = (u8 *)(e1000_rx_ring[e1000_rx_head].addr & ~0xF);
            e1000_rx_ring[e1000_rx_head].status = 0;
            e1000_rx_ring[e1000_rx_head].addr = 0x40120000UL + e1000_rx_head * 2048;
            e1000_rx_head = (e1000_rx_head + 1) % E1000_RING_SIZE;
            e1000_regs[E1000_RDT / 4] = e1000_rx_head;
            (void)length; (void)pkt; /* TODO: pass to network stack */
        }
    }

    if (icr & (1u << 1)) { /* TX interrupt */
        /* Transmit complete */
        while (e1000_tx_ring[e1000_tx_head].status & E1000_DESC_DD) {
            e1000_tx_ring[e1000_tx_head].status = 0;
            e1000_tx_head = (e1000_tx_head + 1) % E1000_RING_SIZE;
        }
    }
}

int e1000_send(const void *data, u32 len) {
    if (e1000_tx_ring[e1000_tx_head].status & E1000_DESC_DD) {
        return -1; /* Ring full */
    }
    e1000_tx_ring[e1000_tx_head].addr = (u64)(u32)data;
    e1000_tx_ring[e1000_tx_head].length = len;
    e1000_tx_ring[e1000_tx_head].status = E1000_DESC_EOP;
    e1000_regs[E1000_TDT / 4] = (e1000_tx_head + 1) % E1000_RING_SIZE;
    e1000_tx_head = (e1000_tx_head + 1) % E1000_RING_SIZE;
    return 0;
}