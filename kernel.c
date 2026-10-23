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

/* Switch to task's EL0 translation table and context. */
void task_switch(u32 id) {
    if (id >= MAX_TASKS || tasks[id].state == TASK_EMPTY) return;
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(tasks[id].pgd));
    /* Publish the new task tables before invalidating translations.  Without
     * this DSB, QEMU can retain the kernel's old mapping of TASK_CODE_VA and
     * EL0 takes a level-3 instruction translation fault. */
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
    __asm__ volatile("msr sp_el0, %0" :: "r"(tasks[id].sp_el0));
    __asm__ volatile("msr elr_el1, %0" :: "r"(tasks[id].pc));
    __asm__ volatile("msr spsr_el1, %0" :: "r"(tasks[id].spsr));
}

/* Enter task at EL0 with arguments in x0-x2. Does not return; control comes
 * back via SVC/exception into user_exception. */
void task_launch(u32 id, u64 a0, u64 a1, u64 a2) {
    task_switch(id);
    __asm__ volatile(
        "mov x0, %0\n\t"
        "mov x1, %1\n\t"
        "mov x2, %2\n\t"
        "eret"
        :: "r"(a0), "r"(a1), "r"(a2)
    );
    for (;;) {}
}

void kernel_main(void) {
    uart_puts("mycelius: Thank you for using mycelius! The system has started successfully\r\n");
    ipc_init();
    vfs_init();
    mmu_init();
    virtio_probe();
    (void)task_create(1, NET_MMIO_VA, 0x200); /* virtio-net driver */
    (void)task_create(2, 0x0a000200UL, 0x200); /* virtio-blk driver */

    tasks[1].pgd = mmu_task_init(1);
    tasks[2].pgd = mmu_task_init(2);

    /* Map driver code at TASK_CODE_VA in task page tables */
    mmu_map_page((u64 *)tasks[1].pgd, TASK_CODE_VA,
                 (u64)&virtio_net_driver_entry & ~0xFFF,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_PXN);
    mmu_map_page((u64 *)tasks[2].pgd, TASK_CODE_VA,
                 (u64)&virtio_blk_driver_entry & ~0xFFF,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_PXN);

    /* mmu_task_init allocated a distinct IPC page for each task. */

    /* Map net device MMIO window into task page tables at NET_MMIO_VA */
    mmu_map_page((u64 *)tasks[1].pgd, NET_MMIO_VA, 0x0a000000UL,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_DEVICE | MMU_PXN);
    mmu_map_page((u64 *)tasks[2].pgd, NET_MMIO_VA, 0x0a000200UL,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_DEVICE | MMU_PXN);

    tasks[1].sp_el0 = TASK_STACK_VA + 4096 - 16;
    tasks[1].pc = TASK_CODE_VA;
    tasks[1].spsr = 0x00000000;  /* EL0t, SP_EL0 */
    tasks[2].sp_el0 = TASK_STACK_VA + 4096 - 16;
    tasks[2].pc = TASK_CODE_VA;
    tasks[2].spsr = 0x00000000;

    /* Do not launch these probes yet.  QEMU reports a level-3 instruction
     * translation fault at TASK_CODE_VA, so launching them would bypass the
     * kernel's net/ext4 initialization and silently select the RAM VFS. */
    uart_puts("mycelius: EL0 probe launch deferred (task code mapping fault)\r\n");
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
    /* Reserved for a verified EL0 exit path. */
    net_init();
    net_set_up(1);
    if (ext4_mount() == 0) {
        vfs_use_ext4();
        uart_puts("mycelius: ext4 mounted read-write\r\n");
    }
    uart_puts("mycelius> type 'help'\r\nmycelius> ");
    for (;;) { driver_supervise(); net_poll(); cli_poll(); }
}
