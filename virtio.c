#include "kernel.h"
/* Modern virtio-MMIO discovery. Queue setup follows after this confirms the
 * physical device inventory; DTB parsing will replace this bounded QEMU scan. */
#define BASE 0x0a000000UL
#define SLOT 0x200UL
#define R(base, off) (*(volatile u32 *)((base) + (off)))
struct device { u64 base; u32 id; };
static struct device devices[8];
static u32 count;
static u8 legacy_queue[8][8192] __attribute__((aligned(4096)));
static u8 sector0[512] __attribute__((aligned(16)));
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
            R(base, 0x028) = 4096; R(base, 0x030) = 0;
            u32 max = R(base, 0x034); if (!max) continue;
            R(base, 0x038) = max < 8 ? max : 8;
            R(base, 0x03c) = 4096;
            R(base, 0x040) = (u32)((u64)&legacy_queue[count][0] >> 12);
            R(base, 0x070) |= 4;
        } else if (version != 2) continue;
        devices[count++] = (struct device){ base, id };
    }
}
void virtio_status(void) {
    if (!count) { uart_puts("virtio: no MMIO devices found; magic=0x"); hex(R(BASE, 0)); uart_puts(" version=0x"); hex(R(BASE, 4)); uart_puts("\r\n"); return; }
    for (u32 i = 0; i < count; ++i) {
        uart_puts("virtio: ");
        uart_puts(devices[i].id == 1 ? "net" : devices[i].id == 2 ? "blk" : "other");
        uart_puts(" device discovered\r\n");
    }
}
static u16 blk_used_seen = 0;

int virtio_blk_read(u64 sector, void *buffer) {
    u32 slot = 8;
    for (u32 i = 0; i < count; ++i) if (devices[i].id == 2) { slot = i; break; }
    if (slot == 8) return -1;
    u8 *q = legacy_queue[slot]; struct desc *d = (struct desc *)q;
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8); /* flags, idx, ring[] */
    volatile u16 *used = (volatile u16 *)(q + 4096);    /* flags, idx, elements */
    request.type = 0; request.reserved = 0; request.sector = sector; request_status = 0xff;
    d[0] = (struct desc){ (u64)&request, 16, 1, 1 };
    d[1] = (struct desc){ (u64)buffer, 512, 1 | 2, 2 }; /* device writes data */
    d[2] = (struct desc){ (u64)&request_status, 1, 2, 0 };
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    R(devices[slot].base, 0x050) = 0; /* QueueNotify */
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] != blk_used_seen) {
            blk_used_seen = used[1];
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
    R(devices[slot].base, 0x050) = 0;
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] != blk_used_seen) {
            blk_used_seen = used[1];
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
    R(devices[slot].base, 0x050) = 0;
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] != blk_used_seen) {
            blk_used_seen = used[1];
            return request_status == 0 ? 0 : -1;
        }
    }
    return -1;
}

int virtio_blk_read0(void) { return virtio_blk_read(0, sector0); }
