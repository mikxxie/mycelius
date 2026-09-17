#pragma once
#include "kernel.h"

#define VIRTIO_MMIO_BASE 0x0a000000UL
#define VIRTIO_MMIO_SLOT 0x200UL
#define VIRTIO_ID_NET 1
#define VIRTIO_ID_BLK 2
#define VIRTQ_SIZE 8
#define VIRTQ_ALIGN 4096
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4
#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_F_FLUSH 9

struct virtq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};

struct virtio_dev {
    u64 base;
    u32 id;
    u32 version;
    u16 qsize;
    u16 used_seen;
    u32 features;
    u8 *queue;
};

u32 virtio_dev_count(void);
struct virtio_dev *virtio_dev_at(u32 i);
struct virtio_dev *virtio_dev_by_id(u32 id);
int virtio_setup_queue0(struct virtio_dev *d, u8 *queue_8k);
int virtio_q0_issue(struct virtio_dev *d, u16 head);
void virtio_mmio_ack(struct virtio_dev *d);
