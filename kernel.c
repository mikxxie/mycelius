#include "kernel.h"

/* PL011 is supplied by QEMU virt.  This is kernel-only diagnostic output. */
#define UART_DR (*(volatile u32 *)0x09000000UL)
#define UART_FR (*(volatile u32 *)0x09000018UL)
#define UART_TXFF (1u << 5)
#define UART_RXFE (1u << 4)
static struct task tasks[MAX_TASKS];
static u8 page_used[RAM_BYTES / PAGE_SIZE / 8]; /* 512 bytes for all 16 MiB */

void uart_puts(const char *s) { while (*s) { while (UART_FR & UART_TXFF) {} UART_DR = (u32)*s++; } }
int uart_getc_nonblock(void) { return (UART_FR & UART_RXFE) ? -1 : (int)(UART_DR & 0xff); }

void *page_alloc(void) {
    for (u32 p = 256; p < RAM_BYTES / PAGE_SIZE; ++p) {
        u32 byte = p / 8, bit = p % 8;
        if (!(page_used[byte] & (1u << bit))) { page_used[byte] |= 1u << bit; return (void *)(RAM_BASE + (u64)p * PAGE_SIZE); }
    }
    return 0;
}

int task_create(u32 id, u64 base, u64 size) {
    if (id >= MAX_TASKS || tasks[id].state != TASK_EMPTY || !size) return -1;
    tasks[id] = (struct task){
        .id = id, .state = TASK_READY, .mmio_base = base, .mmio_size = size,
        .pgd = 0, .sp_el0 = 0, .pc = 0, .spsr = 0
    };
    return 0;
}

void task_fault(u32 id) { if (id < MAX_TASKS && tasks[id].state != TASK_EMPTY) tasks[id].state = TASK_FAULTED; }
const struct task *task_get(u32 id) { return id < MAX_TASKS ? &tasks[id] : 0; }
void task_restart(u32 id) { task_fault(id); driver_supervise(); }
u32 page_free_count(void) {
    u32 free = 0;
    for (u32 p = 256; p < RAM_BYTES / PAGE_SIZE; ++p)
        if (!(page_used[p / 8] & (1u << (p % 8)))) ++free;
    return free;
}

void driver_supervise(void) {
    for (u32 i = 0; i < MAX_TASKS; ++i) if (tasks[i].state == TASK_FAULTED) {
        u64 base = tasks[i].mmio_base, size = tasks[i].mmio_size;
        tasks[i].state = TASK_EMPTY;
        (void)task_create(i, base, size);
        uart_puts("mycelius: restarted driver\r\n");
    }
}

void mmu_map_page(u64 *pgd, u64 va, u64 pa, u64 attr);

void kernel_main(void) {
    uart_puts("mycelius: Thank you for using mycelius! The system has started successfully\r\n");
    ipc_init();
    vfs_init();
    mmu_init();
    virtio_probe();
    (void)task_create(1, 0x0a000000UL, 0x200); /* virtio-net driver */
    (void)task_create(2, 0x0a000200UL, 0x200); /* virtio-blk driver */

    /* Call virtio_net_driver_entry directly at EL1 to test the driver logic */
    uart_puts("mycelius: calling virtio_net_driver_entry at EL1\r\n");
    /* virtio_net_driver_entry(0x0a000000UL, 1, TASK_IPC_VA); */
    uart_puts("mycelius: net driver skipped\r\n");

    uart_puts("mycelius: calling virtio_blk_driver_entry at EL1\r\n");
    /* virtio_blk_driver_entry(0x0a000200UL, 2, TASK_IPC_VA); */
    uart_puts("mycelius: blk driver skipped\r\n");

    net_init();
    net_set_up(1);
    if (ext4_mount() == 0) {
        vfs_use_ext4();
        uart_puts("mycelius: ext4 mounted read-write\r\n");
    }
    uart_puts("mycelius> type 'help'\r\nmycelius> ");
    for (;;) { driver_supervise(); net_poll(); cli_poll(); }
}

void kernel_after_user(void) {
    uart_puts("mycelius: EL0 returned\r\nmycelius> ");
    for (;;) cli_poll();
}
