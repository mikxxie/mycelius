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
            /* Note: DRIVER_OK is NOT set here - it's set by the individual
             * driver after feature negotiation (see virtio_gpu_display) */
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

static u32 net_slot(void) {
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

/* ------------------------------------------------------------------ */
/* Virtio-GPU: minimal display driver                                 */
/* ------------------------------------------------------------------ */
/* Discovers the virtio-gpu device (type 16), sets up queue 0, creates
 * a 2D resource backed by the gui software framebuffer, sets scanout,
 * and flushes pixels so the GUI appears on the QEMU GTK window.       */

#define VIRTIO_ID_GPU 16

/* Virtio-GPU command types (per VirtIO-GPU spec) */
#define GPU_CMD_GET_DISPLAY_INFO   0x0300
#define GPU_CMD_RESOURCE_CREATE_2D 0x0301
#define GPU_CMD_RESOURCE_SET_SCANOUT 0x0302
#define GPU_CMD_RESOURCE_FLUSH     0x0303
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x0304
#define GPU_CMD_TRANSFER_TO_HOST_2D 0x0306

/* Virtio-GPU flags */
#define GPU_FLAG_FENCE (1u << 0)

/* Virtio-GPU response types */
#define GPU_RESP_OK_NODATA   0x1100
#define GPU_RESP_OK_DISPLAYINFO 0x1101

/* Virtio-GPU formats */
#define GPU_FORMAT_BGRX8888 1
#define GPU_FORMAT_RGBA8888 6

/* Virtio-GPU structures (little-endian fields) */
struct gpu_ctrl_hdr {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
};

struct gpu_rect {
    u32 x, y, width, height;
};

struct gpu_resp_hdr {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
};

struct gpu_display_info {
    struct gpu_resp_hdr hdr;
    struct gpu_rect rect;
    u32 enabled;
    u32 flags;
};

struct gpu_resource_create_2d {
    struct gpu_ctrl_hdr hdr;
    u32 resource_id;
    u32 format;
    u32 width;
    u32 height;
};

struct gpu_resource_set_scanout {
    struct gpu_ctrl_hdr hdr;
    struct gpu_rect rect;
    u32 scanout_id;
    u32 resource_id;
};

struct gpu_resource_flush {
    struct gpu_ctrl_hdr hdr;
    struct gpu_rect rect;
    u32 resource_id;
    u32 padding;
};

struct gpu_transfer_to_host_2d {
    struct gpu_ctrl_hdr hdr;
    struct gpu_rect r;
    u64 offset;
    u32 resource_id;
    u32 padding;
};

struct gpu_mem_entry {
    u64 addr;
    u32 length;
    u32 padding;
};

struct gpu_resource_attach_backing {
    struct gpu_ctrl_hdr hdr;
    u32 resource_id;
    u32 num_entries;
    u32 padding;
};

static u32 gpu_notify_off = 0x050; /* legacy MMIO QueueNotify offset */

/* Fill a virtio-gpu control header.
 * Legacy virtio-MMIO (QEMU virt) does not use fence flags, so flags=0.
 */
static void gpu_hdr_init(struct gpu_ctrl_hdr *ch, u32 type) {
    ch->type = type;
    ch->flags = 0;
    ch->fence_id = 0;
    ch->ctx_id = 0;
    ch->padding = 0;
}

int virtio_gpu_display(void) {
    u32 slot = 8;
    for (u32 i = 0; i < count; ++i)
        if (devices[i].id == VIRTIO_ID_GPU) { slot = i; break; }
    if (slot == 8) {
        uart_puts("virtio-gpu: device not found\r\n");
        return -1;
    }

    u64 base = devices[slot].base;

    /* QEMU virt exposes virtio-gpu as legacy MMIO v1. virtio_probe() already
     * set up status=ACK|DRIVER|DRIVER_OK and queue 0 with legacy MMIO offsets.
     * Just use the existing queue. */
    gpu_notify_off = 0x050; /* legacy MMIO QueueNotify offset */

    /* Feature negotiation: read device features low word (offset 0x010 with
     * FeatureSel=0) to find offered features. QEMU offers VIRTIO_F_VERSION_1
     * (bit 28) which we must negotiate before it accepts commands. */
    R(base, 0x014) = 0;              /* FeatureSel = low word */
    u32 feat_lo = R(base, 0x010);    /* Read device features low */
    u32 status;
    uart_puts("virtio-gpu: device features low=0x");
    { char h[9]; u32 v = feat_lo; for (int i = 7; i >= 0; --i) { u32 d = (v >> (i*4)) & 0xF; h[7-i] = (char)(d < 10 ? '0'+d : 'a'+d-10); } h[8] = 0; uart_puts(h); }
    uart_puts("\r\n");

    /* Negotiate VIRTIO_F_VERSION_1 (bit 28) - QEMU requires this */
    if (feat_lo & (1u << 28)) {
        R(base, 0x024) = 0;          /* DriverFeaturesSel = low word */
        R(base, 0x020) = 1u << 28;   /* Set VIRTIO_F_VERSION_1 */
        uart_puts("virtio-gpu: set VIRTIO_F_VERSION_1\r\n");
    } else {
        uart_puts("virtio-gpu: WARNING - VIRTIO_F_VERSION_1 not offered!\r\n");
    }

    /* Set FEATURES_OK, then DRIVER_OK (correct ordering per VirtIO spec) */
    R(base, 0x070) |= 8;             /* FEATURES_OK */
    status = R(base, 0x070);
    if (!(status & 8)) {
        uart_puts("virtio-gpu: FEATURES_OK rejected\r\n");
        return -1;
    }
    uart_puts("virtio-gpu: FEATURES_OK accepted\r\n");

    R(base, 0x070) |= 4;             /* DRIVER_OK */
    status = R(base, 0x070);
    if (!(status & 4)) {
        uart_puts("virtio-gpu: DRIVER_OK rejected\r\n");
        return -1;
    }
    uart_puts("virtio-gpu: DRIVER_OK accepted\r\n");

    /* Re-setup queue after DRIVER_OK — QEMU may require queue configuration
     * after DRIVER_OK before it processes commands. */
    R(base, 0x030) = 0;              /* QueueSel = 0 */
    u32 max = R(base, 0x034);        /* QueueNumMax */
    if (!max) max = 8;
    R(base, 0x038) = max < 8 ? max : 8;
    R(base, 0x03c) = 4096;           /* QueueAlign */
    R(base, 0x040) = (u32)((u64)&legacy_queue[slot][0] >> 12);  /* QueueDesc PFN */

    /* Allocate command/response pages from page_alloc() so we know their
     * physical addresses for DMA. QEMU DMA reads physical addresses, not
     * kernel virtual addresses. */
    u64 cmd_pa = (u64)page_alloc();
    u64 resp_pa = (u64)page_alloc();
    if (!cmd_pa || !resp_pa) {
        uart_puts("virtio-gpu: page_alloc failed\r\n");
        return -1;
    }
    volatile u8 *cmd_buf = (volatile u8 *)cmd_pa;   /* identity-mapped at 0x40000000 */
    volatile u8 *resp_buf = (volatile u8 *)resp_pa;

    u32 fb_pa = gui_fb_addr();
    if (!fb_pa) {
        uart_puts("virtio-gpu: no framebuffer\r\n");
        return -1;
    }

    u32 width = gui_fb_width();
    u32 height = gui_fb_height();
    u32 num_scanouts;

    /* Read GPU config space before any commands (QEMU requires this) */
    R(base, 0x114) = 0;   /* ConfigSel = events_read */
    (void)R(base, 0x110);
    R(base, 0x114) = 1;   /* ConfigSel = events_clear */
    (void)R(base, 0x110);
    R(base, 0x114) = 2;   /* ConfigSel = num_scanouts */
    num_scanouts = R(base, 0x110);
    uart_puts("virtio-gpu: num_scanouts=");
    { char h[2]; u32 v = num_scanouts; h[0] = (char)('0' + v); h[1] = 0; uart_puts(h); }
    uart_puts("\r\n");
    R(base, 0x114) = 3;   /* ConfigSel = num_capsets */
    (void)R(base, 0x110);

    /* Legacy queue layout: descriptors@0, avail@128, used@4096 */
    struct desc *d = (struct desc *)legacy_queue[slot];
    volatile u16 *avail = (volatile u16 *)(legacy_queue[slot] + 16 * 8);
    volatile u16 *used = (volatile u16 *)(legacy_queue[slot] + 4096);

    /* Helper: submit one command and wait for completion */
    #define GPU_CMD(cmd_type, cmd_size, resp_size) do { \
        barrier(); \
        avail[2] = 0; avail[3] = 0; \
        barrier(); \
        avail[1] = 1; \
        barrier(); \
        R(base, gpu_notify_off) = 0; \
        u16 target = used[1] + 1; \
        for (u32 spins = 0; spins < 5000000; ++spins) { \
            barrier(); \
            if (used[1] == target) goto done_##cmd_type; \
        } \
        uart_puts("virtio-gpu: " #cmd_type " timeout\r\n"); \
        return -1; \
        done_##cmd_type:; \
    } while (0)

    /* --- 1. GET_DISPLAY_INFO --- */
    {
        static u64 fence_id = 1;
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)cmd_buf;
        ch->type = GPU_CMD_GET_DISPLAY_INFO;
        ch->flags = GPU_FLAG_FENCE;
        ch->fence_id = fence_id++;
        ch->ctx_id = 0;
        ch->padding = 0;

        d[0] = (struct desc){ cmd_pa, sizeof(struct gpu_ctrl_hdr), 1, 1 };
        d[1] = (struct desc){ resp_pa, sizeof(struct gpu_display_info), 2, 0 };

        GPU_CMD(GET_DISPLAY_INFO, sizeof(struct gpu_ctrl_hdr), sizeof(struct gpu_display_info));

        struct gpu_display_info *disp = (struct gpu_display_info *)resp_buf;
        if (disp->hdr.type != GPU_RESP_OK_DISPLAYINFO) {
            uart_puts("virtio-gpu: GET_DISPLAY_INFO failed, type=");
            { u32 v = disp->hdr.type; char h[9]; for (int i = 7; i >= 0; --i) { u32 dd = (v >> (i*4)) & 0xF; h[7-i] = (char)(dd < 10 ? '0'+dd : 'a'+dd-10); } h[8] = 0; uart_puts(h); }
            uart_puts("\r\n");
            return -1;
        }
        uart_puts("virtio-gpu: GET_DISPLAY_INFO OK\r\n");
        uart_puts("virtio-gpu: display=");
        { char w[6]; u32 v = disp->rect.width; for (int i = 4; i >= 0; --i) { u32 d = (v >> (i*4)) & 0xF; w[4-i] = (char)(d < 10 ? '0'+d : 'a'+d-10); } w[5] = 0; uart_puts(w); }
        uart_puts("x");
        { char h[6]; u32 v = disp->rect.height; for (int i = 4; i >= 0; --i) { u32 d = (v >> (i*4)) & 0xF; h[4-i] = (char)(d < 10 ? '0'+d : 'a'+d-10); } h[5] = 0; uart_puts(h); }
        uart_puts("\r\n");
    }

    /* --- 2. RESOURCE_CREATE_2D --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_CREATE_2D);
        struct gpu_resource_create_2d *rc = (struct gpu_resource_create_2d *)cmd_buf;
        rc->resource_id = 1;
        rc->format = GPU_FORMAT_RGBA8888;
        rc->width = width;
        rc->height = height;

        d[0] = (struct desc){ cmd_pa, sizeof(struct gpu_resource_create_2d), 1, 1 };
        d[1] = (struct desc){ resp_pa, sizeof(struct gpu_resp_hdr), 2, 0 };

        GPU_CMD(RESOURCE_CREATE_2D, sizeof(struct gpu_resource_create_2d), sizeof(struct gpu_resp_hdr));

        struct gpu_resp_hdr *resp = (struct gpu_resp_hdr *)resp_buf;
        if (resp->type != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_CREATE_2D failed\r\n");
            return -1;
        }
        uart_puts("virtio-gpu: RESOURCE_CREATE_2D done\r\n");
    }

    /* --- 3. RESOURCE_ATTACH_BACKING --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_ATTACH_BACKING);
        struct gpu_resource_attach_backing *ab = (struct gpu_resource_attach_backing *)cmd_buf;
        ab->resource_id = 1;
        ab->num_entries = 1;
        ab->padding = 0;
        struct gpu_mem_entry *me = (struct gpu_mem_entry *)(cmd_buf + sizeof(struct gpu_resource_attach_backing));
        me->addr = fb_pa;
        me->length = width * height * 4;
        me->padding = 0;

        u32 ab_size = sizeof(struct gpu_resource_attach_backing) + sizeof(struct gpu_mem_entry);
        d[0] = (struct desc){ cmd_pa, ab_size, 1, 1 };
        d[1] = (struct desc){ resp_pa, sizeof(struct gpu_resp_hdr), 2, 0 };

        GPU_CMD(RESOURCE_ATTACH_BACKING, ab_size, sizeof(struct gpu_resp_hdr));

        struct gpu_resp_hdr *resp = (struct gpu_resp_hdr *)resp_buf;
        if (resp->type != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_ATTACH_BACKING failed\r\n");
            return -1;
        }
        uart_puts("virtio-gpu: RESOURCE_ATTACH_BACKING done\r\n");
    }

    /* --- 4. TRANSFER_TO_HOST_2D (copy pixels to GPU) --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_TRANSFER_TO_HOST_2D);
        struct gpu_transfer_to_host_2d *t = (struct gpu_transfer_to_host_2d *)cmd_buf;
        t->r.x = 0;
        t->r.y = 0;
        t->r.width = width;
        t->r.height = height;
        t->offset = 0;
        t->resource_id = 1;
        t->padding = 0;

        d[0] = (struct desc){ cmd_pa, sizeof(struct gpu_transfer_to_host_2d), 1, 1 };
        d[1] = (struct desc){ resp_pa, sizeof(struct gpu_resp_hdr), 2, 0 };

        GPU_CMD(TRANSFER_TO_HOST_2D, sizeof(struct gpu_transfer_to_host_2d), sizeof(struct gpu_resp_hdr));

        struct gpu_resp_hdr *resp = (struct gpu_resp_hdr *)resp_buf;
        if (resp->type != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: TRANSFER_TO_HOST_2D failed\r\n");
            return -1;
        }
        uart_puts("virtio-gpu: TRANSFER_TO_HOST_2D done\r\n");
    }

    /* --- 5. RESOURCE_SET_SCANOUT --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_SET_SCANOUT);
        struct gpu_resource_set_scanout *ss = (struct gpu_resource_set_scanout *)cmd_buf;
        ss->rect.x = 0;
        ss->rect.y = 0;
        ss->rect.width = width;
        ss->rect.height = height;
        ss->scanout_id = 0;
        ss->resource_id = 1;

        d[0] = (struct desc){ cmd_pa, sizeof(struct gpu_resource_set_scanout), 1, 1 };
        d[1] = (struct desc){ resp_pa, sizeof(struct gpu_resp_hdr), 2, 0 };

        GPU_CMD(RESOURCE_SET_SCANOUT, sizeof(struct gpu_resource_set_scanout), sizeof(struct gpu_resp_hdr));

        struct gpu_resp_hdr *resp = (struct gpu_resp_hdr *)resp_buf;
        if (resp->type != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_SET_SCANOUT failed\r\n");
            return -1;
        }
        uart_puts("virtio-gpu: RESOURCE_SET_SCANOUT done\r\n");
    }

    /* --- 6. RESOURCE_FLUSH --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_FLUSH);
        struct gpu_resource_flush *fl = (struct gpu_resource_flush *)cmd_buf;
        fl->rect.x = 0;
        fl->rect.y = 0;
        fl->rect.width = width;
        fl->rect.height = height;
        fl->resource_id = 1;
        fl->padding = 0;

        d[0] = (struct desc){ cmd_pa, sizeof(struct gpu_resource_flush), 1, 1 };
        d[1] = (struct desc){ resp_pa, sizeof(struct gpu_resp_hdr), 2, 0 };

        GPU_CMD(RESOURCE_FLUSH, sizeof(struct gpu_resource_flush), sizeof(struct gpu_resp_hdr));

        struct gpu_resp_hdr *resp = (struct gpu_resp_hdr *)resp_buf;
        if (resp->type != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_FLUSH failed\r\n");
            return -1;
        }
        uart_puts("virtio-gpu: RESOURCE_FLUSH done\r\n");
    }

    #undef GPU_CMD

    uart_puts("virtio-gpu: framebuffer displayed on scanout 0\r\n");
    return 0;
}
