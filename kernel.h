#pragma once
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define RAM_BASE 0x40000000UL
#define RAM_BYTES (16UL * 1024 * 1024)
#define PAGE_SIZE 4096UL
#define MAX_TASKS 4

enum task_state { TASK_EMPTY, TASK_READY, TASK_WAITING, TASK_FAULTED };
struct task { u32 id; enum task_state state; u64 mmio_base; u64 mmio_size; };

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
void user_cli_start(void);
void user_exception(u64 x0, u64 syscall);
void kernel_after_user(void);
void vfs_init(void);
void vfs_pwd(void);
void vfs_ls(void);
int vfs_ls_path(const char *path);
int vfs_cd(const char *name);
int vfs_mkdir(const char *name);
int vfs_rm(const char *path, int recursive);
int vfs_write_file(const char *path, const void *data, u32 len);
int vfs_cat(const char *path);
void net_status(void);
void net_set_up(int up);
void net_init(void);
void net_poll(void);
int net_ping(const char *ip);
int virtio_net_mac(u8 *mac);
void virtio_net_debug(void);
int virtio_net_rx(void *buf, u32 cap);
int virtio_net_tx(const void *frame, u32 len);
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
void ipc_init(void);
int ipc_send(u32 from, u32 to, u32 type, u64 value);
int ipc_recv(u32 to, u32 *from, u32 *type, u64 *value);
void virtio_net_driver(void);
void virtio_blk_driver(void);
