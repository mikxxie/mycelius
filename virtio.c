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
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8); /* flags, idx, ring[] */
    volatile u16 *used = (volatile u16 *)(q + 4096);    /* flags, idx, elements */
    request.type = 0; request.reserved = 0; request.sector = sector; request_status = 0xff;
    d[0] = (struct desc){ (u64)&request, 16, 1, 1 };
    d[1] = (struct desc){ (u64)buffer, 512, 1 | 2, 2 }; /* device writes data */
    d[2] = (struct desc){ (u64)&request_status, 1, 2, 0 };
    u16 index = avail[1]; avail[2 + (index & 7)] = 0; barrier(); avail[1] = index + 1;
    barrier();
    u16 target = used[1] + 1;
    R(devices[slot].base, 0x050) = 0; /* QueueNotify */
    for (u32 spins = 0; spins < 2000000; ++spins) {
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

/* Virtio-GPU command types */
#define GPU_CMD_GET_DISPLAY_INFO   0
#define GPU_CMD_RESOURCE_CREATE_2D 1
#define GPU_CMD_RESOURCE_SET_SCANOUT 2
#define GPU_CMD_RESOURCE_FLUSH     3
#define GPU_CMD_RESOURCE_ATTACH_BACKING 4
#define GPU_CMD_TRANSFER_TO_HOST_2D 6

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

/* Command and response pages for virtio-gpu control structures */
static u8 gpu_cmd_buf[4096] __attribute__((aligned(4096)));
static u8 gpu_resp_buf[4096] __attribute__((aligned(4096)));
static u64 gpu_next_fence = 1;

/* Send a single virtio-gpu command.  desc[0] = request (device reads),
 * desc[1] = response (device writes).  Returns the response type. */
static u32 gpu_exec(u64 base, u8 *q) {
    volatile u16 *avail = (volatile u16 *)(q + 16 * 8);
    volatile u16 *used = (volatile u16 *)(q + 4096);

    u16 index = avail[1];
    avail[2 + (index & 7)] = 0; /* chain head = descriptor 0 */
    barrier();
    avail[1] = index + 1;
    barrier();
    R(base, 0x050) = 0; /* QueueNotify for queue 0 */

    u16 target = used[1] + 1;
    for (u32 spins = 0; spins < 2000000; ++spins) {
        barrier();
        if (used[1] == target) {
            struct gpu_resp_hdr *resp = (struct gpu_resp_hdr *)gpu_resp_buf;
            return resp->type;
        }
    }
    return 0xFFFFFFFF; /* timeout */
}

/* Fill a virtio-gpu control header with fence flag + incrementing fence ID. */
static void gpu_hdr_init(struct gpu_ctrl_hdr *ch, u32 type) {
    ch->type = type;
    ch->flags = 1; /* VIRTIO_GPU_FLAG_FENCE */
    ch->fence_id = gpu_next_fence++;
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
    u8 *q = legacy_queue[slot];

    /* --- Virtio device setup (legacy/MMIO, QEMU virt) --- */
    /* virtio_probe() already set: status=ACK|DRIVER|DRIVER_OK, queue rings.
     * Just add FEATURES_OK and GuestPageSize. */
    R(base, 0x070) = 0x0F;           /* Status: ACK|DRIVER|DRIVER_OK|FEATURES_OK */
    R(base, 0x018) = 4096;           /* GuestPageSize */

    u32 fb_pa = gui_fb_addr();
    if (!fb_pa) {
        uart_puts("virtio-gpu: no framebuffer\r\n");
        return -1;
    }

    u32 width = gui_fb_width();
    u32 height = gui_fb_height();

    struct desc *d = (struct desc *)q;

    /* --- 1. GET_DISPLAY_INFO --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)gpu_cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_GET_DISPLAY_INFO);

        d[0] = (struct desc){ (u64)gpu_cmd_buf, sizeof(struct gpu_ctrl_hdr), 1, 1 };
        d[1] = (struct desc){ (u64)gpu_resp_buf, sizeof(struct gpu_display_info), 2, 0 };

        u32 resp = gpu_exec(base, q);
        if (resp != GPU_RESP_OK_DISPLAYINFO) {
            uart_puts("virtio-gpu: GET_DISPLAY_INFO failed, resp=");
            { char hex[9]; u32 v = resp; for (int i = 7; i >= 0; --i) { u32 dd = (v >> (i*4)) & 0xF; hex[7-i] = (char)(dd < 10 ? '0'+dd : 'a'+dd-10); } hex[8] = 0; uart_puts(hex); }
            uart_puts("\r\n");
            return -1;
        }

        struct gpu_display_info *di = (struct gpu_display_info *)gpu_resp_buf;
        uart_puts("virtio-gpu: display ");
        { char hex[5]; u32 v = di->rect.width; hex[0]=(char)(v/1000+'0'); hex[1]=(char)((v/100)%10+'0'); hex[2]=(char)((v/10)%10+'0'); hex[3]=(char)(v%10+'0'); hex[4]=0; uart_puts(hex); }
        uart_puts("x");
        { char hex[5]; u32 v = di->rect.height; hex[0]=(char)(v/1000+'0'); hex[1]=(char)((v/100)%10+'0'); hex[2]=(char)((v/10)%10+'0'); hex[3]=(char)(v%10+'0'); hex[4]=0; uart_puts(hex); }
        uart_puts("\r\n");
    }

    /* --- 2. RESOURCE_CREATE_2D --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)gpu_cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_CREATE_2D);
        struct gpu_resource_create_2d *rc = (struct gpu_resource_create_2d *)gpu_cmd_buf;
        rc->resource_id = 1;
        rc->format = GPU_FORMAT_RGBA8888;
        rc->width = width;
        rc->height = height;

        d[0] = (struct desc){ (u64)gpu_cmd_buf, sizeof(struct gpu_resource_create_2d), 1, 1 };
        d[1] = (struct desc){ (u64)gpu_resp_buf, sizeof(struct gpu_resp_hdr), 2, 0 };

        u32 resp = gpu_exec(base, q);
        if (resp != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_CREATE_2D failed\r\n");
            return -1;
        }
    }

    /* --- 3. RESOURCE_ATTACH_BACKING --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)gpu_cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_ATTACH_BACKING);
        struct gpu_resource_attach_backing *ab = (struct gpu_resource_attach_backing *)gpu_cmd_buf;
        ab->resource_id = 1;
        ab->num_entries = 1;
        ab->padding = 0;
        struct gpu_mem_entry *me = (struct gpu_mem_entry *)(gpu_cmd_buf + sizeof(struct gpu_resource_attach_backing));
        me->addr = fb_pa;
        me->length = width * height * 4;
        me->padding = 0;

        u32 ab_size = sizeof(struct gpu_resource_attach_backing) + sizeof(struct gpu_mem_entry);
        d[0] = (struct desc){ (u64)gpu_cmd_buf, ab_size, 1, 1 };
        d[1] = (struct desc){ (u64)gpu_resp_buf, sizeof(struct gpu_resp_hdr), 2, 0 };

        u32 resp = gpu_exec(base, q);
        if (resp != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_ATTACH_BACKING failed\r\n");
            return -1;
        }
    }

    /* --- 4. RESOURCE_SET_SCANOUT --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)gpu_cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_SET_SCANOUT);
        struct gpu_resource_set_scanout *ss = (struct gpu_resource_set_scanout *)gpu_cmd_buf;
        ss->rect.x = 0;
        ss->rect.y = 0;
        ss->rect.width = width;
        ss->rect.height = height;
        ss->scanout_id = 0;
        ss->resource_id = 1;

        d[0] = (struct desc){ (u64)gpu_cmd_buf, sizeof(struct gpu_resource_set_scanout), 1, 1 };
        d[1] = (struct desc){ (u64)gpu_resp_buf, sizeof(struct gpu_resp_hdr), 2, 0 };

        u32 resp = gpu_exec(base, q);
        if (resp != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_SET_SCANOUT failed\r\n");
            return -1;
        }
    }

    /* --- 5. RESOURCE_FLUSH --- */
    {
        struct gpu_ctrl_hdr *ch = (struct gpu_ctrl_hdr *)gpu_cmd_buf;
        gpu_hdr_init(ch, GPU_CMD_RESOURCE_FLUSH);
        struct gpu_resource_flush *fl = (struct gpu_resource_flush *)gpu_cmd_buf;
        fl->rect.x = 0;
        fl->rect.y = 0;
        fl->rect.width = width;
        fl->rect.height = height;
        fl->resource_id = 1;
        fl->padding = 0;

        d[0] = (struct desc){ (u64)gpu_cmd_buf, sizeof(struct gpu_resource_flush), 1, 1 };
        d[1] = (struct desc){ (u64)gpu_resp_buf, sizeof(struct gpu_resp_hdr), 2, 0 };

        u32 resp = gpu_exec(base, q);
        if (resp != GPU_RESP_OK_NODATA) {
            uart_puts("virtio-gpu: RESOURCE_FLUSH failed\r\n");
            return -1;
        }
    }

    uart_puts("virtio-gpu: framebuffer displayed on scanout 0\r\n");
    return 0;
}
