#pragma once
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#ifndef NULL
#define NULL ((void*)0)
#endif

void put_u32(u32 n);

#define RAM_BASE 0x40000000UL
#define RAM_BYTES (16UL * 1024 * 1024)
#define PAGE_SIZE 4096UL
#define MAX_TASKS 4

/* PL011 is supplied by QEMU virt.  This is kernel-only diagnostic output. */
#define UART_DR (*(volatile u32 *)0x09000000UL)
#define UART_FR (*(volatile u32 *)0x09000018UL)
#define UART_TXFF (1u << 5)
#define UART_RXFE (1u << 4)

/* EL0 task virtual address layout. Each task maps only its code, IPC page,
 * stack, MMIO window, and its virtio queue + data pages.
 * Kernel RAM is present as an EL1-only mapping for exception vector access. */
#define TASK_CODE_VA   0x00400000UL   /* EL0 code (one 4KiB page) */
#define TASK_STACK_VA  0x00402000UL   /* EL0 stack (one 4KB page) */
#define TASK_IPC_VA    0x00404000UL   /* IPC mailbox page (EL0 RW) */
#define NET_MMIO_VA    0x00403000UL   /* MMIO window (one 4KB page) */
/* Driver-owned virtio queue pages: two 8KB rings (queue0 + queue1 for net). */
#define TASK_QUEUE0_VA 0x00500000UL   /* queue ring 0, page 0 of 2 */
#define TASK_QUEUE1_VA 0x00502000UL   /* net tx ring, page 0 of 2 */
#define TASK_DATA_VA   0x00504000UL   /* 512-byte sector / packet data buffer */
#define TASK_REQ_VA    0x00505000UL   /* blk_header request struct + status byte */

/* IPC message types (aligns with dev/ipc.h) */
#define MYCELIUS_MSG_NONE        0u
#define MYCELIUS_MSG_ARP_REPLY   1u   /* net driver -> kernel: ARP reply received */
#define MYCELIUS_MSG_BLK_REQUEST   2u   /* driver -> kernel: block I/O request / status */
#define MYCELIUS_MSG_NET_RX      3u   /* net driver -> kernel: Ethernet frame received */
#define MYCELIUS_MSG_MAX         255u

/* IPC shared page layout (at TASK_IPC_VA physical = task_ipc_pa[id]).
 * The kernel writes a request; the driver reads it, executes, then writes
 * the reply in the same page.  A "ready" flag tells the driver a request
 * is pending; the driver clears it when the reply is posted. */
#define IPC_FLAG_IDLE  0u
#define IPC_FLAG_REQ   1u   /* kernel posted a request */
#define IPC_FLAG_DONE  2u   /* driver posted the reply  */

/* IPC request types used in ipc_page.req_type. */
#define DRV_REQ_NONE   0u
#define DRV_REQ_READ   1u   /* blk: sector read  */
#define DRV_REQ_WRITE  2u   /* blk: sector write */
#define DRV_REQ_FLUSH  3u   /* blk: cache flush  */
#define DRV_REQ_TX     4u   /* net: transmit frame */
#define DRV_REQ_RX     5u   /* net: receive frame  */
#define DRV_REQ_EXIT   6u   /* tear-down: driver should SVC exit */

/* Layout of the IPC page.  The kernel pre-fills the setup section before
 * launching the driver; the driver uses those PAs for virtio descriptors.
 * Both sides access this page: kernel via PA (identity-mapped RAM), driver
 * via TASK_IPC_VA.  Keep the struct under 4 KiB. */
struct ipc_page {
    volatile u32 flag;          /* IPC_FLAG_* */
    volatile u32 req_type;      /* DRV_REQ_* */
    volatile u64 sector;        /* blk: sector number */
    volatile u32 data_len;      /* net: frame length in/out */
    volatile u32 result;        /* driver reply: 0=ok, non-zero=error */
    /* Setup section (written by kernel before launch, read by driver) */
    volatile u64 queue0_pa;     /* PA of driver's queue0 ring (8KB) */
    volatile u64 queue1_pa;     /* PA of driver's queue1 ring (net tx, 8KB) */
    volatile u64 data_pa;       /* PA of driver's data page (512 B) */
    volatile u64 req_pa;        /* PA of driver's req page (blk_header+stat) */
};

enum task_state { TASK_EMPTY, TASK_READY, TASK_WAITING, TASK_FAULTED };
struct task {
    u32 id;
    enum task_state state;
    u64 pgd;             /* physical address of task L0 page table */
    u64 sp_el0;          /* saved stack pointer */
    u64 pc;              /* saved program counter */
    u64 spsr;            /* saved PSTATE on EL0 entry */
    u64 mmio_base;       // MMIO base physical address
    u64 mmio_size;       /* MMIO size in bytes */
};

/* Page table attribute bits (match mmu.c) */
#define MMU_P  3UL
#define MMU_AF (1UL << 10)
#define MMU_SH (3UL << 8)
#define MMU_U  (1UL << 6)
#define MMU_PXN (1UL << 53)
#define MMU_UXN (1UL << 54)
#define MMU_DEVICE (1UL << 2) /* MAIR attribute index 1: device nGnRE */

void uart_puts(const char *s);
int uart_getc_nonblock(void);
void *page_alloc(void);
int task_create(u32 id, u64 mmio_base, u64 mmio_size);
void task_fault(u32 id);
void driver_supervise(void);
const struct task *task_get(u32 id);
void task_restart(u32 id);
u32 page_free_count(void);
void cli_poll(void);
void mmu_init(void);
u64 mmu_task_init(u32 id);           /* build per-task EL0 translation tables, returns pgd */
void task_switch(u32 id);            /* switch TTBR0_EL1 + SP_EL0 to task */
void task_launch(u32 id, u64 a0, u64 a1, u64 a2); /* enter task at EL0 with args */
void mmu_map_page(u64 *pgd, u64 va, u64 pa, u64 attr); /* map a page into a task table */
extern u64 task_code_pa[MAX_TASKS], task_stack_pa[MAX_TASKS], task_ipc_pa[MAX_TASKS];
u32 task_current_id(void);           /* current task id for IPC */
u64 task_current_pgd(void);          /* current task's page table root */

void task_inject_fault(u32 id);      /* inject test fault into driver task */

void vfs_init(void);
void vfs_pwd(void);
void vfs_ls(void);
int vfs_ls_path(const char *path);
int vfs_cd(const char *name);
int vfs_mkdir(const char *name);
int vfs_rm(const char *path, int recursive);
int vfs_write_file(const char *path, const void *data, u32 len);
int vfs_cat(const char *path);
void ext4_pwd(void);
void ipc_init(void);
int ipc_send(u32 from, u32 to, u32 type, u64 value);
int ipc_recv(u32 to, u32 *from, u32 *type, u64 *value);
int ipc_has_message(u32 to);
void virtio_net_driver(void);
void virtio_blk_driver(void);
void virtio_net_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va);
void virtio_blk_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va);
void virtio_probe(void);
void virtio_status(void);
u64 virtio_device_base(u32 device_type_id);
u64 virtio_queue_pa(u32 device_type_id);
int virtio_blk_read0(void);
int virtio_blk_read(u64 sector, void *buffer);
int virtio_blk_write(u64 sector, const void *buffer);
int virtio_blk_flush(void);
void vfs_use_ext4(void);
int ext4_probe(void);
int ext4_mount(void);
int ext4_mounted(void);
int ext4_ls_root(void);
int ext4_ls(void);
int ext4_ls_path(const char *path);
int ext4_cd(const char *path);
int ext4_mkdir(const char *path);
int ext4_rm(const char *path, int recursive);
int ext4_write_file(const char *path, const u8 *data, u32 len);
int ext4_cat(const char *path);
void ext4_pwd(void);
void net_status(void);
void net_set_up(int up);
void net_init(void);
int net_is_up(void);
void net_poll(void);
int net_ping(const char *ip);
int virtio_net_mac(u8 *mac);
void virtio_net_debug(void);
int virtio_net_rx(void *buf, u32 cap);
int virtio_net_tx(const void *frame, u32 len);
void user_cli_start(void);
void kernel_after_user(void);
void gui_init(void);
void gui_demo(void);
u32 gui_fb_addr(void);
u32 gui_fb_width(void);
u32 gui_fb_height(void);
int virtio_gpu_display(void);
int mycc_main(void);
int discoveryd_main(int argc, char **argv);
int moad_main(int argc, char **argv);
