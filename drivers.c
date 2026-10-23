#include "kernel.h"

/* ======================================================================== *
 * EL0 driver entry points                                                   *
 *                                                                           *
 * Each driver runs at EL0 in its own isolated address space.  Arguments:   *
 *   x0 = mmio_va   — MMIO window mapped at NET_MMIO_VA (0x00403000)        *
 *   x1 = task_id   — this driver's task ID                                 *
 *   x2 = ipc_va    — IPC page at TASK_IPC_VA (0x00404000)                  *
 *                                                                           *
 * Before launch, the kernel pre-fills ipc_page PA fields with the physical  *
 * addresses of the driver's queue / data / request pages so EL0 code can   *
 * build correct DMA descriptors (the device sees physical addresses).       *
 *                                                                           *
 * SVC calling convention: x8 = syscall number, x0-x3 = arguments.         *
 *   4 = exit  (driver done → kernel_after_user)                            *
 *   5 = ipc_send(from, to, type, value)                                    *
 *                                                                           *
 * SVC asm is expanded inline in each driver to avoid out-of-page calls.    *
 * ======================================================================== */

/* ======================================================================== *
 * virtio-net driver                                                          *
 * Reads device ID and MAC from MMIO config space, reports to kernel.        *
 * ======================================================================== */
__attribute__((section(".driver_text"), aligned(4096), noinline, noclone))
void virtio_net_driver_entry(u64 mmio_va, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio = (volatile u32 *)mmio_va;
    volatile u32 *ipc  = (volatile u32 *)ipc_va;

    /* Device ID at MMIO offset 0x008 */
    u32 dev_id = mmio[0x008 / 4];
    ipc[0] = dev_id;

    /* MAC address from net config space (offsets 0x100, 0x104) */
    ipc[1] = mmio[0x100 / 4];
    ipc[2] = mmio[0x104 / 4] & 0xffff;

    /* ipc_send(task_id, 0, 4=DRV_READY, dev_id) — inlined SVC */
    __asm__ volatile(
        "mov x0, %0\n\t"
        "mov x1, #0\n\t"
        "mov x2, #4\n\t"
        "mov x3, %1\n\t"
        "mov x8, #5\n\t"
        "svc #0\n\t"
        :: "r"((u64)task_id), "r"((u64)dev_id) : "x0","x1","x2","x3","x8"
    );

    /* Driver service loop: checks IPC page for request commands */
    for (;;) {
        if (ipc[0] == IPC_FLAG_REQ) {
            u32 req = ipc[1];
            if (req == DRV_REQ_EXIT) {
                ipc[0] = IPC_FLAG_IDLE;
                break;
            }
        }
        /* Initial probe phase exits cleanly to allow shell start */
        break;
    }

    /* exit(4) — inlined SVC */
    __asm__ volatile("mov x8, #4\n\t" "svc #0\n\t" ::: "x8");
    for (;;) {}
}

/* ======================================================================== *
 * virtio-blk driver                                                          *
 *                                                                           *
 * Reads PA setup from IPC page, builds a virtio descriptor chain, issues   *
 * a sector-0 read, and reports the result to the kernel via IPC.            *
 *                                                                           *
 * IPC page layout (volatile u64 array):                                     *
 *   [0] flag(u32) | req_type(u32)   [1] sector(u64)                        *
 *   [2] data_len(u32) | result(u32) [3] queue0_pa(u64)                     *
 *   [4] queue1_pa(u64)              [5] data_pa(u64)                        *
 *   [6] req_pa(u64)                                                          *
 * ======================================================================== */
struct drv_desc    { u64 addr; u32 len; u16 flags; u16 next; };
struct drv_blk_hdr { u32 type; u32 reserved; u64 sector; };

__attribute__((section(".driver_text"), aligned(4096), noinline, noclone))
void virtio_blk_driver_entry(u64 mmio_va, u64 task_id, u64 ipc_va) {
    volatile u32 *mmio  = (volatile u32 *)mmio_va;
    volatile u32 *ipc32 = (volatile u32 *)ipc_va;
    volatile u64 *ipc64 = (volatile u64 *)ipc_va;

    /* Confirm device ID */
    ipc32[0] = mmio[0x008 / 4];
    ipc32[1] = (u32)task_id;

    /* Read physical addresses from IPC setup section (pre-filled by kernel) */
    u64 q0_pa   = ipc64[3];  /* offset 0x18: queue0_pa */
    u64 data_pa = ipc64[5];  /* offset 0x28: data_pa   */
    u64 req_pa  = ipc64[6];  /* offset 0x30: req_pa    */

    /* Build descriptor chain using TASK_QUEUE0_VA for ring access (EL0 VA) */
    struct drv_desc    *d    = (struct drv_desc    *)TASK_QUEUE0_VA;
    volatile u16       *avail = (volatile u16      *)(TASK_QUEUE0_VA + 16 * 8);
    volatile u16       *used  = (volatile u16      *)(TASK_QUEUE0_VA + 0x1000);
    struct drv_blk_hdr *hdr  = (struct drv_blk_hdr *)TASK_REQ_VA;
    volatile u8        *stat = (volatile u8        *)(TASK_REQ_VA + 16);
    volatile u8        *data = (volatile u8        *)TASK_DATA_VA;

    /* Zero data buffer */
    for (u32 i = 0; i < 512; ++i) data[i] = 0;

    hdr->type     = 0;   /* VIRTIO_BLK_T_IN */
    hdr->reserved = 0;
    hdr->sector   = 0;
    *stat         = 0xff;

    /* Descriptors: physical addresses for device DMA */
    d[0].addr = req_pa;      d[0].len = 16;  d[0].flags = 1;    d[0].next = 1; /* NEXT */
    d[1].addr = data_pa;     d[1].len = 512; d[1].flags = 1|2;  d[1].next = 2; /* NEXT|WRITE */
    d[2].addr = req_pa + 16; d[2].len = 1;   d[2].flags = 2;    d[2].next = 0; /* WRITE */

    /* Re-point QueuePFN to the driver's own queue page (overrides kernel probe) */
    mmio[0x030 / 4] = 0;               /* QueueSel = 0 */
    mmio[0x040 / 4] = (u32)(q0_pa >> 12); /* QueuePFN */

    /* Push into available ring */
    u16 idx  = avail[1];
    avail[2 + (idx & 7)] = 0;
    __asm__ volatile("dmb ish" ::: "memory");
    avail[1] = idx + 1;
    __asm__ volatile("dmb ish" ::: "memory");

    /* QueueNotify(0) */
    mmio[0x050 / 4] = 0;

    /* Bounded wait */
    u16 used_seen = used[1];
    for (u32 s = 0; s < 4000000; ++s) {
        __asm__ volatile("dmb ish" ::: "memory");
        if (used[1] != used_seen) { used_seen = used[1]; break; }
    }

    /* Report status and first bytes of data */
    ipc32[2] = (u32)*stat;
    ipc32[3] = (u32)data[0] | ((u32)data[1]<<8) | ((u32)data[2]<<16) | ((u32)data[3]<<24);

    /* ipc_send(task_id, 0, 2=BLK_REQUEST, status) — inlined SVC */
    __asm__ volatile(
        "mov x0, %0\n\t"
        "mov x1, #0\n\t"
        "mov x2, #2\n\t"
        "mov x3, %1\n\t"
        "mov x8, #5\n\t"
        "svc #0\n\t"
        :: "r"((u64)task_id), "r"((u64)*stat) : "x0","x1","x2","x3","x8"
    );

    /* Driver service loop: checks IPC page for request commands */
    for (;;) {
        if (ipc32[0] == IPC_FLAG_REQ) {
            u32 req = ipc32[1];
            if (req == DRV_REQ_EXIT) {
                ipc32[0] = IPC_FLAG_IDLE;
                break;
            }
        }
        break;
    }

    /* exit(4) */
    __asm__ volatile("mov x8, #4\n\t" "svc #0\n\t" ::: "x8");
    for (;;) {}
}

void virtio_net_driver(void) { (void)virtio_net_driver_entry; }
void virtio_blk_driver(void) { (void)virtio_blk_driver_entry; }
