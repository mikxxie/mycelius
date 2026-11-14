#include "virtio.h"

/* Framebuffer from gui.c */
extern u32 fb_buf[];
#define FB_WIDTH  320
#define FB_HEIGHT 240
#define FB_BPP    4

#define R(base, off) (*(volatile u32 *)((base) + (off)))

static struct virtio_dev devices[8];
static u32 count;

static void barrier(void) { __asm__ volatile("dmb ish" ::: "memory"); }

void put_u32(u32 n) {
    char digits[11];
    u32 i = 0;
    if (!n) { uart_puts("0"); return; }
    while (n) { digits[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) { char out[2] = { digits[--i], 0 }; uart_puts(out); }
}

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

/* ======================================================================== */
/* VirtIO GPU driver                                                        */
/* ======================================================================== */

#define VIRTIO_ID_GPU 16

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO   0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_SET_SCANOUT        0x0103
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0102
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH     0x0107

struct virtio_gpu_ctrl_hdr {
    u32 type;
    u32 flags;
    u64 fence_id;
};

struct virtio_gpu_resp {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
};

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_resp hdr;
    u32 rect_width;
    u32 rect_height;
    u32 enabled;
    u32 flags;
};

struct virtio_gpu_mem_entry {
    u64 addr;
    u32 length;
    u32 padding;
};

struct virtio_gpu_rect {
    u32 x, y, width, height;
};

struct virtio_gpu_resource_create_2d {
    u32 resource_id;
    u32 width;
    u32 height;
    u32 format; /* 1 = B8G8R8A8_UNORM */
};

struct virtio_gpu_resource_attach_backing {
    u32 resource_id;
    u32 num_entries;
    u32 padding;
};

struct virtio_gpu_set_scanout {
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    u32 resource_id;
};

/* Framebuffer buffer from gui.c */
extern u32 fb_buf[];

int virtio_gpu_display(void) {
    struct virtio_dev *gpu = virtio_dev_by_id(VIRTIO_ID_GPU);
    if (!gpu) {
        uart_puts("gpu: no virtio-gpu device found\r\n");
        return -1;
    }

    u64 base = gpu->base;

    /* 1. Reset device */
    R(base, 0x040) = 0;
    barrier();

    /* 2. ACKNOWLEDGE */
    R(base, 0x040) = 1;
    barrier();

    /* 3. DRIVER */
    R(base, 0x040) = 1 | 2;
    barrier();

    /* 4. Read features, set guest features */
    u32 host_features = R(base, 0x010);
    u32 guest_features = host_features & ~(1u << 0); /* Don't need VIRTIO_F_NOTIFY_ON_EMPTY */
    R(base, 0x014) = guest_features;
    gpu->features = guest_features;

    /* 5. FEATURES_OK */
    R(base, 0x040) = 1 | 2 | 8;
    barrier();

    /* 6. Set up control queue (queue 0) */
    static u8 gpu_queue[8192] __attribute__((aligned(4096)));
    if (virtio_setup_queue0(gpu, gpu_queue) < 0) {
        uart_puts("gpu: queue setup failed\r\n");
        return -1;
    }

    /* 7. DRIVER_OK */
    R(base, 0x040) = 1 | 2 | 8 | 4;
    barrier();

    uart_puts("gpu: device initialized\r\n");

    /* 8. GET_DISPLAY_INFO */
    static struct virtio_gpu_ctrl_hdr chdr __attribute__((aligned(16)));
    static struct virtio_gpu_resp_display_info resp __attribute__((aligned(16)));

    chdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    chdr.flags = 0;
    chdr.fence_id = 0;

    /* Build descriptor chain */
    volatile struct virtq_desc *desc = (volatile struct virtq_desc *)gpu_queue;
    desc[0].addr = (u64)&chdr;
    desc[0].len = sizeof(struct virtio_gpu_ctrl_hdr);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (u64)&resp;
    desc[1].len = sizeof(struct virtio_gpu_resp_display_info);
    desc[1].flags = VIRTQ_DESC_F_WRITE;
    desc[1].next = 0;

    /* Push to available ring */
    volatile u16 *avail = (volatile u16 *)(gpu_queue + 16u * gpu->qsize);
    volatile u16 *used_idx = (volatile u16 *)(gpu_queue + VIRTQ_ALIGN + 2);
    u16 mask = (u16)(gpu->qsize - 1);
    u16 aidx = avail[1];
    avail[2 + (aidx & mask)] = 0; /* head = 0 */
    barrier();
    avail[1] = (u16)(aidx + 1);
    barrier();
    R(base, 0x050) = 0; /* QueueNotify(0) */

    /* Wait for completion */
    u16 useen = used_idx[0];
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != useen) {
            virtio_mmio_ack(gpu);
            break;
        }
    }

    uart_puts("gpu: display info: ");
    put_u32(resp.rect_width);
    uart_puts("x");
    put_u32(resp.rect_height);
    uart_puts("\r\n");

    /* 9. RESOURCE_CREATE_2D */
    static struct virtio_gpu_resource_create_2d create2d __attribute__((aligned(16)));
    create2d.resource_id = 1;
    create2d.width = 320;
    create2d.height = 240;
    create2d.format = 1; /* B8G8R8A8_UNORM */

    desc[0].addr = (u64)&create2d;
    desc[0].len = sizeof(struct virtio_gpu_resource_create_2d);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (u64)&resp;
    desc[1].len = sizeof(struct virtio_gpu_resp);
    desc[1].flags = VIRTQ_DESC_F_WRITE;
    desc[1].next = 0;

    avail[2 + (avail[1] & mask)] = 0;
    barrier();
    avail[1] = (u16)(avail[1] + 1);
    barrier();
    R(base, 0x050) = 0;

    useen = used_idx[0];
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != useen) {
            virtio_mmio_ack(gpu);
            break;
        }
    }

    uart_puts("gpu: resource created\r\n");

    /* 10. RESOURCE_ATTACH_BACKING */
    static struct virtio_gpu_resource_attach_backing attach __attribute__((aligned(16)));
    static struct virtio_gpu_mem_entry entry __attribute__((aligned(16)));
    attach.resource_id = 1;
    attach.num_entries = 1;
    attach.padding = 0;
    entry.addr = (u64)fb_buf;
    entry.length = FB_WIDTH * FB_HEIGHT * FB_BPP;
    entry.padding = 0;

    desc[0].addr = (u64)&attach;
    desc[0].len = sizeof(struct virtio_gpu_resource_attach_backing);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (u64)&entry;
    desc[1].len = sizeof(struct virtio_gpu_mem_entry);
    desc[1].flags = VIRTQ_DESC_F_NEXT;
    desc[1].next = 2;

    desc[2].addr = (u64)&resp;
    desc[2].len = sizeof(struct virtio_gpu_resp);
    desc[2].flags = VIRTQ_DESC_F_WRITE;
    desc[2].next = 0;

    avail[2 + (avail[1] & mask)] = 0;
    barrier();
    avail[1] = (u16)(avail[1] + 1);
    barrier();
    R(base, 0x050) = 0;

    useen = used_idx[0];
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != useen) {
            virtio_mmio_ack(gpu);
            break;
        }
    }

    uart_puts("gpu: backing attached\r\n");

    /* 11. SET_SCANOUT */
    static struct virtio_gpu_set_scanout scanout __attribute__((aligned(16)));
    scanout.x = 0;
    scanout.y = 0;
    scanout.width = 320;
    scanout.height = 240;
    scanout.resource_id = 1;

    desc[0].addr = (u64)&scanout;
    desc[0].len = sizeof(struct virtio_gpu_set_scanout);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (u64)&resp;
    desc[1].len = sizeof(struct virtio_gpu_resp);
    desc[1].flags = VIRTQ_DESC_F_WRITE;
    desc[1].next = 0;

    avail[2 + (avail[1] & mask)] = 0;
    barrier();
    avail[1] = (u16)(avail[1] + 1);
    barrier();
    R(base, 0x050) = 0;

    useen = used_idx[0];
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != useen) {
            virtio_mmio_ack(gpu);
            break;
        }
    }

    uart_puts("gpu: scanout set\r\n");

    /* 12. TRANSFER_TO_HOST_2D */
    static struct virtio_gpu_rect rect __attribute__((aligned(16)));
    rect.x = 0;
    rect.y = 0;
    rect.width = 320;
    rect.height = 240;

    desc[0].addr = (u64)&rect;
    desc[0].len = sizeof(struct virtio_gpu_rect);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (u64)&resp;
    desc[1].len = sizeof(struct virtio_gpu_resp);
    desc[1].flags = VIRTQ_DESC_F_WRITE;
    desc[1].next = 0;

    avail[2 + (avail[1] & mask)] = 0;
    barrier();
    avail[1] = (u16)(avail[1] + 1);
    barrier();
    R(base, 0x050) = 0;

    useen = used_idx[0];
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != useen) {
            virtio_mmio_ack(gpu);
            break;
        }
    }

    uart_puts("gpu: frame transferred\r\n");

    /* 13. RESOURCE_FLUSH */
    desc[0].addr = (u64)&rect;
    desc[0].len = sizeof(struct virtio_gpu_rect);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (u64)&resp;
    desc[1].len = sizeof(struct virtio_gpu_resp);
    desc[1].flags = VIRTQ_DESC_F_WRITE;
    desc[1].next = 0;

    avail[2 + (avail[1] & mask)] = 0;
    barrier();
    avail[1] = (u16)(avail[1] + 1);
    barrier();
    R(base, 0x050) = 0;

    useen = used_idx[0];
    for (u32 spins = 0; spins < 4000000u; ++spins) {
        barrier();
        if (*used_idx != useen) {
            virtio_mmio_ack(gpu);
            break;
        }
    }

    uart_puts("gpu: flush complete - display active\r\n");
    return 0;
}

int virtio_net_tx(const void *buf, u32 len) { (void)buf; (void)len; return -1; }
int virtio_net_rx(void *buf, u32 cap) { (void)buf; (void)cap; return -1; }
int virtio_net_mac(u8 *mac) { (void)mac; return -1; }
void virtio_net_debug(void) { uart_puts("virtio-net: debug not implemented\r\n"); }
int virtio_blk_read(u64 sector, void *buffer) { (void)sector; (void)buffer; return -1; }
int virtio_blk_write(u64 sector, const void *buffer) { (void)sector; (void)buffer; return -1; }
int virtio_blk_flush(void) { return -1; }
int virtio_blk_read0(void) { return -1; }

int net_is_up(void) { return 0; }

void virtio_net_driver_entry(u64 mmio_va, u64 task_id, u64 ipc_va) {
    (void)mmio_va; (void)task_id; (void)ipc_va;
    uart_puts("virtio-net: driver stub (no network)\r\n");
}

void virtio_blk_driver_entry(u64 mmio_va, u64 task_id, u64 ipc_va) {
    (void)mmio_va; (void)task_id; (void)ipc_va;
    uart_puts("virtio-blk: driver stub (no block device)\r\n");
}
