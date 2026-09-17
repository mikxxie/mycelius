#include "virtio.h"

static u8 queue[8192] __attribute__((aligned(4096)));
static struct virtio_dev *blk;
static struct {
    u32 type;
    u32 reserved;
    u64 sector;
} req __attribute__((aligned(16)));
static volatile u8 req_status;
static u8 sector0[512] __attribute__((aligned(16)));
static int ready;

static int xfer(u32 type, u64 sector, void *data, u32 len) {
    if (!ready || !blk) return -1;
    struct virtq_desc *d = (struct virtq_desc *)blk->queue;
    req.type = type;
    req.reserved = 0;
    req.sector = sector;
    req_status = 0xff;
    if (type == VIRTIO_BLK_T_FLUSH || !data || !len) {
        d[0] = (struct virtq_desc){ (u64)&req, 16, VIRTQ_DESC_F_NEXT, 1 };
        d[1] = (struct virtq_desc){ (u64)&req_status, 1, VIRTQ_DESC_F_WRITE, 0 };
        if (virtio_q0_issue(blk, 0)) return -1;
        return req_status == VIRTIO_BLK_S_OK ? 0 : -1;
    }
    /* Device writes the data buffer on read (IN); it only reads it on write. */
    u16 data_flags = VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
    d[0] = (struct virtq_desc){ (u64)&req, 16, VIRTQ_DESC_F_NEXT, 1 };
    d[1] = (struct virtq_desc){ (u64)data, len, data_flags, 2 };
    d[2] = (struct virtq_desc){ (u64)&req_status, 1, VIRTQ_DESC_F_WRITE, 0 };
    if (virtio_q0_issue(blk, 0)) return -1;
    return req_status == VIRTIO_BLK_S_OK ? 0 : -1;
}

void virtio_blk_driver(void) {
    blk = virtio_dev_by_id(VIRTIO_ID_BLK);
    ready = 0;
    if (!blk) {
        uart_puts("mycelius: virtio-blk device not found\r\n");
        return;
    }
    if (virtio_setup_queue0(blk, queue)) {
        uart_puts("mycelius: virtio-blk queue setup failed\r\n");
        return;
    }
    ready = 1;
    uart_puts("mycelius: virtio-blk driver registered\r\n");
}

int virtio_blk_ready(void) { return ready; }
int virtio_blk_read(u64 sector, void *buffer) { return xfer(VIRTIO_BLK_T_IN, sector, buffer, 512); }
int virtio_blk_write(u64 sector, const void *buffer) { return xfer(VIRTIO_BLK_T_OUT, sector, (void *)buffer, 512); }
int virtio_blk_flush(void) { return xfer(VIRTIO_BLK_T_FLUSH, 0, 0, 0); }
int virtio_blk_read0(void) { return virtio_blk_read(0, sector0); }
