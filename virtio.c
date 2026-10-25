#include "kernel.h"
/* Modern virtio-MMIO discovery. Queue setup follows after this confirms the
 * physical device inventory; DTB parsing will replace this bounded QEMU scan. */
#define BASE 0x0a000000UL
#define SLOT 0x200UL
#define R(base, off) (*(volatile u32 *)((base) + (off)))
struct device { u64 base; u32 id; u32 queue_idx; };
static struct device devices[8];
static u32 count;
static u8 legacy_queue[8][8192] __attribute__((aligned(4096)));
static u8 legacy_tx_queue[8][8192] __attribute__((aligned(4096)));
static u8 sector0[512] __attribute__((aligned(16)));
static u8 net_tx_hdr[10] __attribute__((aligned(16)));
static volatile u8 net_tx_status;
static u16 net_tx_seen;
static u16 net_rx_seen;
struct desc { u64 addr; u32 len; u16 flags, next; };
struct blk_header { u32 type, reserved; u64 sector; };
static struct blk_header request __attribute__((aligned(16)));
static u8 request_status;
static void barrier(void) { __asm__ volatile("dmb ish" ::: "memory"); }
static void hex(u32 v) { char out[2] = { 0, 0 }; for (int i = 7; i >= 0; --i) { u32 d = (v >> (i * 4)) & 15; out[0] = (char)(d < 10 ? '0' + d : 'a' + d - 10); uart_puts(out); } }
void virtio_probe(void) {
    count = 0;
    for (u32 i = 0; i < 32 && count < 8; ++i) {
        u64 base = BASE + (u64)i * SLOT;
        /* magic "virt", version 2, and a non-zero device ID are mandatory. */
        if (R(base, 0x000) != 0x74726976 || !R(base, 0x008)) continue;
        u32 version = R(base, 0x004), id = R(base, 0x008);
        /* QEMU's default virtio-mmio is legacy version 1.  Queue zero uses
         * two contiguous 4 KiB pages: descriptor/avail then page-aligned used. */
        if (version == 1) {
            R(base, 0x070) = 0; R(base, 0x070) = 1 | 2;
            R(base, 0x028) = 4096;
            u32 max = R(base, 0x034); if (!max) continue;
            R(base, 0x030) = 0;
            R(base, 0x038) = max < 8 ? max : 8;
            R(base, 0x03c) = 4096;
            R(base, 0x040) = (u32)((u64)&legacy_queue[count][0] >> 12);
            if (id == 1) { /* legacy virtio-net: queue 1 is transmit */
                R(base, 0x030) = 1;
                R(base, 0x038) = max < 8 ? max : 8;
                R(base, 0x03c) = 4096;
                R(base, 0x040) = (u32)((u64)&legacy_tx_queue[count][0] >> 12);
            }
            R(base, 0x070) |= 4;  /* DRIVER_OK */
            devices[count++] = (struct device){ base, id, count };
            continue;
        } else if (version == 2) {
            /* modern v2: full setup */
            R(base, 0x070) = 0;                  /* Reset */
            R(base, 0x070) = 1 | 2;              /* ACK | DRIVER */
            R(base, 0x028) = 4096;               /* GuestPageSize */
            R(base, 0x030) = 0;                  /* QueueSel = 0 */
            u32 max = R(base, 0x034);            /* QueueNumMax */
            if (!max) continue;
            R(base, 0x038) = max < 8 ? max : 8;
            R(base, 0x03c) = 16;                 /* QueueAlign */
            u64 qpa = (u64)&legacy_queue[count][0];
            R(base, 0x080) = (u32)qpa;           /* QueueDescLow */
            R(base, 0x084) = (u32)(qpa >> 32);   /* QueueDescHigh */
            R(base, 0x090) = (u32)((u64)&legacy_queue[count][128]);   /* QueueDriverLow */
            R(base, 0x094) = (u32)((u64)&legacy_queue[count][128] >> 32); /* QueueDriverHigh */
            R(base, 0x0A0) = (u32)((u64)&legacy_queue[count][4096]);   /* QueueDeviceLow */
            R(base, 0x0A4) = (u32)((u64)&legacy_queue[count][4096] >> 32); /* QueueDeviceHigh */
            R(base, 0x044) = 1;                  /* QueueReady */
            devices[count++] = (struct device){ base, id, count };
            continue;
        } else continue;
    }
}
void virtio_status(void) {
    if (!count) { uart_puts("virtio: no MMIO devices found; magic=0x"); hex(R(BASE, 0)); uart_puts(" version=0x"); hex(R(BASE, 4)); uart_puts("\r\n"); return; }
    for (u32 i = 0; i < count; ++i) {
        uart_puts("virtio: ");
        uart_puts(devices[i].id == 1 ? "net" : devices[i].id == 2 ? "blk" : "other");
        uart_puts(" device discovered (version ");
        hex(R(devices[i].base, 0x004));
        uart_puts(")\r\n");
    }
}

u64 virtio_device_base(u32 device_type_id) {
    for (u32 i = 0; i < count; ++i)
        if (devices[i].id == device_type_id) return devices[i].base;
    return 0;
}

u64 virtio_queue_pa(u32 device_type_id) {
    for (u32 i = 0; i < count; ++i)
        if (devices[i].id == device_type_id) return (u64)&legacy_queue[i][0];
    return 0;
}
static u16 blk_used_seen = 0;

int virtio_blk_read(u64 sector, void *buffer) {
    u32 slot = 8;
    for (u32 i = 0; i < count; ++i) if (devices[i].id == 2) { slot = i; break; }
    if (slot == 8) return -1;
    u8 *q = legacy_queue[slot]; struct desc *d = (struct desc *)q;
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8);
    volatile u16 *used = (volatile u16 *)(q + 4096);
    request.type = 0; request.reserved = 0; request.sector = sector; request_status = 0xff;
    d[0] = (struct desc){ (u64)&request, 16, 1, 1 };
    d[1] = (struct desc){ (u64)buffer, 512, 1 | 2, 2 };
    d[2] = (struct desc){ (u64)&request_status, 1, 2, 0 };
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    u16 target = used[1] + 1;
    R(devices[slot].base, 0x050) = 0;
    u32 spins;
    for (spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] == target) {
            blk_used_seen = target;
            return request_status == 0 ? 0 : -1;
        }
    }
    return -1;
}

int virtio_blk_write(u64 sector, const void *buffer) {
    u32 slot = 8;
    for (u32 i = 0; i < count; ++i) if (devices[i].id == 2) { slot = i; break; }
    if (slot == 8) return -1;
    u8 *q = legacy_queue[slot]; struct desc *d = (struct desc *)q;
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8);
    volatile u16 *used = (volatile u16 *)(q + 4096);
    request.type = 1; request.reserved = 0; request.sector = sector; request_status = 0xff;
    d[0] = (struct desc){ (u64)&request, 16, 1, 1 };
    d[1] = (struct desc){ (u64)buffer, 512, 1, 2 }; /* device reads data */
    d[2] = (struct desc){ (u64)&request_status, 1, 2, 0 };
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    u16 target = used[1] + 1;
    R(devices[slot].base, 0x050) = 0;
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] == target) {
            blk_used_seen = target;
            return request_status == 0 ? 0 : -1;
        }
    }
    return -1;
}

int virtio_blk_flush(void) {
    u32 slot = 8;
    for (u32 i = 0; i < count; ++i) if (devices[i].id == 2) { slot = i; break; }
    if (slot == 8) return -1;
    u8 *q = legacy_queue[slot]; struct desc *d = (struct desc *)q;
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8);
    volatile u16 *used = (volatile u16 *)(q + 4096);
    request.type = 4; request.reserved = 0; request.sector = 0; request_status = 0xff;
    d[0] = (struct desc){ (u64)&request, 16, 1, 1 };
    d[1] = (struct desc){ (u64)&request_status, 1, 2, 0 };
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    u16 target = used[1] + 1;
    R(devices[slot].base, 0x050) = 0;
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] == target) {
            blk_used_seen = target;
            return request_status == 0 ? 0 : -1;
        }
    }
    return -1;
}

int virtio_blk_read0(void) { return virtio_blk_read(0, sector0); }

static u16 net_slot(void) {
    for (u32 i = 0; i < count; ++i) if (devices[i].id == 1) return i;
    return 8;
}

void virtio_net_debug(void) {
    for (u32 i = 0; i < count; ++i) {
        u64 base = devices[i].base;
        uart_puts("virtio: id=");
        hex(devices[i].id);
        uart_puts(" status=");
        hex(R(base, 0x070));
        uart_puts("\r\n");
    }
}

int virtio_net_mac(u8 *mac) {
    u32 slot = net_slot();
    if (slot == 8) return -1;
    u32 lo = R(devices[slot].base, 0x100), hi = R(devices[slot].base, 0x104);
    mac[0] = (u8)(lo & 0xff); mac[1] = (u8)((lo >> 8) & 0xff);
    mac[2] = (u8)((lo >> 16) & 0xff); mac[3] = (u8)((lo >> 24) & 0xff);
    mac[4] = (u8)(hi & 0xff); mac[5] = (u8)((hi >> 8) & 0xff);
    return 0;
}

int virtio_net_rx(void *buf, u32 cap) {
    u32 slot = net_slot();
    if (slot == 8) return -1;
    u8 *q = legacy_queue[slot]; struct desc *d = (struct desc *)q;
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8);
    volatile u16 *used = (volatile u16 *)(q + 4096);
    d[0] = (struct desc){ (u64)buf, cap, 2, 0 }; /* device writes hdr + frame */
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    R(devices[slot].base, 0x050) = 0; /* notify rx queue */
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] != net_rx_seen) {
            net_rx_seen = used[1];
            /* used ring: flags@0, idx@2, then ring[] of {u32 id, u32 len}
             * starting at byte 4; the current completion is ring[(idx-1)%8]. */
            u16 elem = (used[1] - 1) & 7;
            return (int)used[4 + elem * 4] | ((int)used[5 + elem * 4] << 16);
        }
    }
    return -1;
}

int virtio_net_tx(const void *frame, u32 len) {
    u32 slot = net_slot();
    if (slot == 8) return -1;
    u8 *q = legacy_tx_queue[slot]; struct desc *d = (struct desc *)q;
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8);
    volatile u16 *used = (volatile u16 *)(q + 4096);
    for (u32 i = 0; i < 10; ++i) net_tx_hdr[i] = 0;
    net_tx_status = 0xff;
    d[0] = (struct desc){ (u64)net_tx_hdr, 10, 1, 1 };
    d[1] = (struct desc){ (u64)frame, len, 1, 2 };
    d[2] = (struct desc){ (u64)&net_tx_status, 1, 2, 0 };
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    R(devices[slot].base, 0x050) = 1; /* notify tx queue */
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] != net_tx_seen) {
            net_tx_seen = used[1];
            return 0; /* used-ring advance confirms the frame was transmitted */
        }
    }
    return -1;
}

/* Virtio-GPU: minimal display driver (reports framebuffer address)
 * QEMU 7.2.22 virtio-gpu does not accept commands in legacy MMIO mode,
 * so the flat framebuffer at fb_pa is reported instead. The demo pattern
 * is drawn by gui_demo() in gui.c.                                    */

#define VIRTIO_ID_GPU 16

int virtio_gpu_display(void) {
    u32 slot = 8;
    for (u32 i = 0; i < count; ++i)
        if (devices[i].id == VIRTIO_ID_GPU) { slot = i; break; }
    if (slot == 8) {
        uart_puts("virtio-gpu: device not found\r\n");
        return -1;
    }

    u32 fb_pa = gui_fb_addr();
    if (!fb_pa) {
        uart_puts("virtio-gpu: no framebuffer\r\n");
        return -1;
    }

    u32 width = gui_fb_width();
    u32 height = gui_fb_height();

    uart_puts("virtio-gpu: framebuffer at 0x");
    for (int i = 28; i >= 0; i -= 4) {
        u32 d = (u32)((fb_pa >> i) & 0xf);
        char c[2] = { d < 10 ? '0' + d : 'a' + d - 10, 0 };
        uart_puts(c);
    }
    uart_puts(" (");

    { char w[6]; u32 v = width; for (int i = 4; i >= 0; --i) { u32 d = (v >> (i*4)) & 0xF; w[4-i] = (char)(d < 10 ? '0'+d : 'a'+d-10); } w[5] = 0; uart_puts(w); }
    uart_puts("x");
    { char h[6]; u32 v = height; for (int i = 4; i >= 0; --i) { u32 d = (v >> (i*4)) & 0xF; h[4-i] = (char)(d < 10 ? '0'+d : 'a'+d-10); } h[5] = 0; uart_puts(h); }
    uart_puts("x32)\r\n");

    uart_puts("virtio-gpu: flat framebuffer ready for scanout\r\n");
    return 0;
}
