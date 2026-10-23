#include "kernel.h"

/* PL011 is supplied by QEMU virt.  This is kernel-only diagnostic output. */
#define UART_DR (*(volatile u32 *)0x09000000UL)
#define UART_FR (*(volatile u32 *)0x09000018UL)
#define UART_TXFF (1u << 5)
#define UART_RXFE (1u << 4)
extern u64 l0[512];
static struct task tasks[MAX_TASKS];
static u8 page_used[RAM_BYTES / PAGE_SIZE / 8]; /* bitmap: 512 bytes covers 16 MiB */
static u32 el0_boot_phase;
static u32 current_task;

u64 task_current_pgd(void) {
    if (current_task < MAX_TASKS && tasks[current_task].state != TASK_EMPTY)
        return tasks[current_task].pgd;
    return (u64)l0;
}

void uart_puts(const char *s) { while (*s) { while (UART_FR & UART_TXFF) {} UART_DR = (u32)*s++; } }
int uart_getc_nonblock(void) { return (UART_FR & UART_RXFE) ? -1 : (int)(UART_DR & 0xff); }

void *page_alloc(void) {
    for (u32 p = 256; p < RAM_BYTES / PAGE_SIZE; ++p) {
        u32 byte = p / 8, bit = p % 8;
        if (!(page_used[byte] & (1u << bit))) {
            page_used[byte] |= 1u << bit;
            void *pa = (void *)(RAM_BASE + (u64)p * PAGE_SIZE);
            /* Zero the page so EL0 never sees stale kernel data. */
            for (u32 i = 0; i < PAGE_SIZE / 8; ++i) ((u64*)pa)[i] = 0;
            return pa;
        }
    }
    return 0;
}

int task_create(u32 id, u64 base, u64 size) {
    if (id >= MAX_TASKS || tasks[id].state != TASK_EMPTY || !size) return -1;
    tasks[id] = (struct task){
        .id = id, .state = TASK_READY, .mmio_base = base, .mmio_size = size,
        .pgd = 0, .sp_el0 = 0, .pc = 0, .spsr = 0,
        .queue0_pa = 0, .queue1_pa = 0, .data_pa = 0, .req_pa = 0
    };
    return 0;
}

void task_fault(u32 id)  { if (id < MAX_TASKS && tasks[id].state != TASK_EMPTY) tasks[id].state = TASK_FAULTED; }
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

/* Switch TTBR0_EL1 to this task's per-task page table root, then load the
 * saved context into EL1 system registers so ERET drops to EL0. */
void task_switch(u32 id) {
    if (id >= MAX_TASKS || tasks[id].state == TASK_EMPTY) return;
    current_task = id;
    /* Use the task's own root — provides full isolation. */
    mmu_install_task(tasks[id].pgd);
    __asm__ volatile("msr sp_el0,   %0" :: "r"(tasks[id].sp_el0));
    __asm__ volatile("msr elr_el1,  %0" :: "r"(tasks[id].pc));
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

/* Allocate and map two queue pages (8 KiB ring) for a task at the given VA.
 * Returns the physical address of page 0 (stored in task.queue0_pa etc.). */
static u64 alloc_queue_pages(u32 id, u64 va_page0) {
    u64 pa0 = (u64)page_alloc();
    u64 pa1 = (u64)page_alloc();
    if (!pa0 || !pa1) return 0;
    mmu_map_page((u64 *)tasks[id].pgd, va_page0,        pa0, MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_UXN);
    mmu_map_page((u64 *)tasks[id].pgd, va_page0 + 0x1000, pa1, MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_UXN);
    return pa0;
}

void kernel_main(void) {
    uart_puts("mycelius: Thank you for using mycelius! The system has started successfully\r\n");
    ipc_init();
    vfs_init();
    mmu_init();
    virtio_probe();

    /* ------------------------------------------------------------------ */
    /* Task 1: virtio-net driver (MMIO at 0x0a000000, size 0x200)          */
    /* ------------------------------------------------------------------ */
    (void)task_create(1, 0x0a000000UL, 0x200);
    tasks[1].pgd = mmu_task_init(1);

    /* Map driver code at TASK_CODE_VA */
    mmu_map_page((u64 *)tasks[1].pgd, TASK_CODE_VA,
                 (u64)&virtio_net_driver_entry & ~0xFFFUL,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_PXN);
    /* Map MMIO at NET_MMIO_VA */
    mmu_map_page((u64 *)tasks[1].pgd, NET_MMIO_VA, 0x0a000000UL,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_DEVICE | MMU_PXN | MMU_UXN);
    /* Allocate driver-owned virtio rings: queue0 (rx) and queue1 (tx) */
    tasks[1].queue0_pa = alloc_queue_pages(1, TASK_QUEUE0_VA);
    tasks[1].queue1_pa = alloc_queue_pages(1, TASK_QUEUE1_VA);
    /* Allocate data page (packet buffer) and request page */
    tasks[1].data_pa = (u64)page_alloc();
    mmu_map_page((u64 *)tasks[1].pgd, TASK_DATA_VA, tasks[1].data_pa,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_UXN);
    tasks[1].req_pa = (u64)page_alloc();
    mmu_map_page((u64 *)tasks[1].pgd, TASK_REQ_VA, tasks[1].req_pa,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_UXN);

    tasks[1].sp_el0 = TASK_STACK_VA + PAGE_SIZE - 16;
    tasks[1].pc     = TASK_CODE_VA;
    tasks[1].spsr   = 0x00000000; /* EL0t */

    /* ------------------------------------------------------------------ */
    /* Task 2: virtio-blk driver (MMIO at 0x0a000200, size 0x200)          */
    /* ------------------------------------------------------------------ */
    (void)task_create(2, 0x0a000200UL, 0x200);
    tasks[2].pgd = mmu_task_init(2);

    mmu_map_page((u64 *)tasks[2].pgd, TASK_CODE_VA,
                 (u64)&virtio_blk_driver_entry & ~0xFFFUL,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_PXN);
    mmu_map_page((u64 *)tasks[2].pgd, NET_MMIO_VA, 0x0a000200UL,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_DEVICE | MMU_PXN | MMU_UXN);
    /* blk uses only queue0 (no tx queue) */
    tasks[2].queue0_pa = alloc_queue_pages(2, TASK_QUEUE0_VA);
    tasks[2].data_pa   = (u64)page_alloc();
    mmu_map_page((u64 *)tasks[2].pgd, TASK_DATA_VA, tasks[2].data_pa,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_UXN);
    tasks[2].req_pa = (u64)page_alloc();
    mmu_map_page((u64 *)tasks[2].pgd, TASK_REQ_VA, tasks[2].req_pa,
                 MMU_P | MMU_AF | MMU_SH | MMU_U | MMU_UXN);

    tasks[2].sp_el0 = TASK_STACK_VA + PAGE_SIZE - 16;
    tasks[2].pc     = TASK_CODE_VA;
    tasks[2].spsr   = 0x00000000;

    net_init();
    net_set_up(1);
    if (ext4_mount() == 0) {
        vfs_use_ext4();
        uart_puts("mycelius: ext4 mounted read-write\r\n");
    }

    /* Pre-fill each driver's IPC page with the physical addresses of its
     * queue/data/req pages so the EL0 code can build correct DMA descriptors. */
    { struct ipc_page *np = (struct ipc_page *)task_ipc_pa[1];
      np->queue0_pa = tasks[1].queue0_pa;
      np->queue1_pa = tasks[1].queue1_pa;
      np->data_pa   = tasks[1].data_pa;
      np->req_pa    = tasks[1].req_pa;
      np->flag      = IPC_FLAG_IDLE;
    }
    { struct ipc_page *bp = (struct ipc_page *)task_ipc_pa[2];
      bp->queue0_pa = tasks[2].queue0_pa;
      bp->queue1_pa = 0;
      bp->data_pa   = tasks[2].data_pa;
      bp->req_pa    = tasks[2].req_pa;
      bp->flag      = IPC_FLAG_IDLE;
    }

    /* Launch net driver: reads device ID, initialises rings, exits via SVC. */
    uart_puts("mycelius: launching net driver at EL0\r\n");
    task_launch(1, NET_MMIO_VA, 1, TASK_IPC_VA);
    for (;;) {}
}

void kernel_after_user(void) {
    if (el0_boot_phase++ == 0) {
        uart_puts("mycelius: net driver returned\r\n");
        uart_puts("mycelius: launching blk driver at EL0\r\n");
        task_launch(2, NET_MMIO_VA, 2, TASK_IPC_VA);
    } else {
        uart_puts("mycelius: blk driver returned\r\n");
        uart_puts("mycelius> type 'help'\r\nmycelius> ");
        for (;;) { driver_supervise(); net_poll(); cli_poll(); }
    }
}
