#pragma once
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define RAM_BASE 0x40000000UL
#define RAM_BYTES (16UL * 1024 * 1024)
#define PAGE_SIZE 4096UL
#define MAX_TASKS 4

/* EL0 task virtual address layout. Each task maps only its code, IPC page,
 * stack, and assigned MMIO window, plus kernel RAM needed for exception
 * handling. */
#define TASK_CODE_VA 0x00400000UL
#define TASK_STACK_VA 0x00402000UL
#define TASK_IPC_VA   0x00404000UL
#define NET_MMIO_VA   0x00403000UL

enum task_state { TASK_EMPTY, TASK_READY, TASK_WAITING, TASK_FAULTED };
struct task {
    u32 id;
    enum task_state state;
    u64 pgd;             /* physical address of task L0 page table */
    u64 sp_el0;          /* saved stack pointer */
    u64 pc;              /* saved program counter */
    u64 spsr;            /* saved PSTATE on EL0 entry */
    u64 mmio_base;       // MMIO base address
    u64 mmio_size;       // MMIO size
};

/* Page table attribute bits (match mmu.c) */
#define MMU_P  1UL
#define MMU_AF (1UL << 10)
#define MMU_SH (3UL << 8)
#define MMU_U  (1UL << 6)
#define MMU_PXN (1UL << 53)
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
void mmu_task_activate(u32 id, u64 code_pa, u64 mmio_pa);
void task_switch(u32 id);            /* switch TTBR0_EL1 + SP_EL0 to task */
void task_launch(u32 id, u64 a0, u64 a1, u64 a2); /* enter task at EL0 with args */
void mmu_map_page(u64 *pgd, u64 va, u64 pa, u64 attr); /* map a page into a task table */
extern u64 task_code_pa[MAX_TASKS], task_stack_pa[MAX_TASKS], task_ipc_pa[MAX_TASKS];

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
void virtio_net_driver(void);
void virtio_blk_driver(void);
void virtio_net_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va);
void virtio_blk_driver_entry(u64 mmio_base, u64 task_id, u64 ipc_va);
void virtio_probe(void);
void virtio_status(void);
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
void net_poll(void);
int net_ping(const char *ip);
int virtio_net_mac(u8 *mac);
void virtio_net_debug(void);
int virtio_net_rx(void *buf, u32 cap);
int virtio_net_tx(const void *frame, u32 len);
void user_cli_start(void);

void kernel_after_user(void);
