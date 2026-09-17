#include "virtio.h"

#define R(base, off) (*(volatile u32 *)((base) + (off)))

static struct virtio_dev devices[8];
static u32 count;

static void barrier(void) { __asm__ volatile("dmb ish" ::: "memory"); }

static void hex(u32 v) {
    char out[2] = { 0, 0 };
    for (int i = 7; i >= 0; --i) {
        u32 d = (v >> (i * 4)) & 15;
        out[0] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        uart_puts(out);
    }
}

u32 virtio_dev_count(void) { return count; }
struct virtio_dev *virtio_dev_at(u32 i) { return i < count ? &devices[i] : 0; }
struct virtio_dev *virtio_dev_by_id(u32 id) {
    for (u32 i = 0; i < count; ++i) if (devices[i].id == id) return &devices[i];
    return 0;
}

void virtio_probe(void) {
    count = 0;
    for (u32 i = 0; i < 32 && count < 8; ++i) {
        u64 base = VIRTIO_MMIO_BASE + (u64)i * VIRTIO_MMIO_SLOT;
        if (R(base, 0x000) != 0x74726976 || !R(base, 0x008)) continue;
        u32 version = R(base, 0x004), id = R(base, 0x008);
        /* QEMU virt defaults to legacy MMIO (version 1). Modern v2 is skipped
         * until a split virtqueue layout is wired for that transport. */
        if (version != 1) continue;
        devices[count++] = (struct virtio_dev){ base, id, version, 0, 0, 0, 0 };
    }
}

void virtio_status(void) {
    if (!count) {
        uart_puts("virtio: no MMIO devices found; magic=0x");
        hex(R(VIRTIO_MMIO_BASE, 0));
        uart_puts(" version=0x");
        hex(R(VIRTIO_MMIO_BASE, 4));
        uart_puts("\r\n");
        return;
    }
    for (u32 i = 0; i < count; ++i) {
        uart_puts("virtio: ");
        uart_puts(devices[i].id == VIRTIO_ID_NET ? "net" : devices[i].id == VIRTIO_ID_BLK ? "blk" : "other");
        uart_puts(" device discovered\r\n");
    }
}

int virtio_setup_queue0(struct virtio_dev *d, u8 *queue_8k) {
    if (!d || d->version != 1 || !queue_8k) return -1;
    u64 base = d->base;
    for (u32 i = 0; i < 8192; ++i) queue_8k[i] = 0;
    R(base, 0x070) = 0;
    for (u32 spins = 0; R(base, 0x070) != 0; ++spins) if (spins > 100000) return -1;
    R(base, 0x070) = 1;
    R(base, 0x070) = 1 | 2;
    R(base, 0x028) = 4096;
    u32 host = R(base, 0x010);
    u32 guest = 0;
    if (d->id == VIRTIO_ID_BLK && (host & (1u << VIRTIO_BLK_F_FLUSH))) guest |= (1u << VIRTIO_BLK_F_FLUSH);
    R(base, 0x024) = 0;
    R(base, 0x020) = guest;
    d->features = guest;
    R(base, 0x030) = 0;
    u32 max = R(base, 0x034);
    if (!max) return -1;
    u32 qsize = max < VIRTQ_SIZE ? max : VIRTQ_SIZE;
    /* Split virtqueues require a power-of-two ring. */
    if (qsize >= 8) qsize = 8;
    else if (qsize >= 4) qsize = 4;
    else if (qsize >= 2) qsize = 2;
    else return -1;
    R(base, 0x038) = qsize;
    R(base, 0x03c) = VIRTQ_ALIGN;
    R(base, 0x040) = (u32)((u64)queue_8k >> 12);
    R(base, 0x070) = 1 | 2 | 4;
    d->qsize = (u16)qsize;
    d->used_seen = 0;
    d->queue = queue_8k;
    return 0;
}

void virtio_mmio_ack(struct virtio_dev *d) {
    if (!d) return;
    u32 isr = R(d->base, 0x060);
    if (isr) R(d->base, 0x064) = isr;
}

int virtio_q0_issue(struct virtio_dev *d, u16 head) {
    if (!d || !d->queue || !d->qsize) return -1;
    u8 *q = d->queue;
    volatile u16 *avail = (volatile u16 *)(q + 16u * d->qsize);
    volatile u16 *used_idx = (volatile u16 *)(q + VIRTQ_ALIGN + 2);
    u16 mask = (u16)(d->qsize - 1);
    u16 aidx = avail[1];
    avail[2 + (aidx & mask)] = head;
    barrier();
    avail[1] = (u16)(aidx + 1);
    barrier();
    R(d->base, 0x050) = 0;
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != d->used_seen) {
            d->used_seen = *used_idx;
            virtio_mmio_ack(d);
            return 0;
        }
    }
    return -1;
}
