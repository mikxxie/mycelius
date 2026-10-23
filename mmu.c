#include "kernel.h"
/* TCR: T0SZ=16 (48-bit VA), IRGN0=WB, ORGN0=WB, SH0=IS, TG0=4KB */
#define T 3UL     /* table descriptor */
#define B 1UL     /* block descriptor */
#define P 3UL     /* page descriptor */
#define AF (1UL << 10)
#define SH (3UL << 8)
#define U (1UL << 6)     /* AP[1]: EL0 accessible */
#define PXN (1UL << 53)  /* privileged execute-never */
#define UXN (1UL << 54)  /* unprivileged execute-never */

/* Kernel EL1 page tables — shared for TTBR0 (low) and TTBR1 (high). */
u64 l0[512]  __attribute__((aligned(4096)));
u64 l1a[512] __attribute__((aligned(4096)));
u64 l2a[512] __attribute__((aligned(4096)));
u64 l2r[512] __attribute__((aligned(4096)));
u64 l3u[512] __attribute__((aligned(4096)));  /* kept for compatibility */

/* Per-task EL0 translation tables.  Each task has its own root (task_pgd)
 * installed as TTBR0_EL1 on task_switch(), giving fully isolated address
 * spaces.  All driver pages live in the 2 MiB block [0x400000..0x5FFFFF]:
 *
 *   L3 index | VA           | Contents
 *   ---------+--------------+---------
 *       0    | 0x00400000   | code  (EL0 RX)
 *       2    | 0x00402000   | stack (EL0 RW)
 *       3    | 0x00403000   | MMIO  (EL0 RW device)
 *       4    | 0x00404000   | IPC   (EL0 RW)
 *     256    | 0x00500000   | queue0 page 0 (desc/avail)
 *     257    | 0x00501000   | queue0 page 1 (used ring)
 *     258    | 0x00502000   | queue1 page 0 (net tx desc/avail)
 *     259    | 0x00503000   | queue1 page 1 (net tx used ring)
 *     260    | 0x00504000   | data  (sector/packet buffer, EL0 RW)
 *     261    | 0x00505000   | req   (blk_header + status, EL0 RW)
 *
 * task_l1[id][1] -> l2r gives EL1 the kernel RAM block for exception
 * handling without switching page tables. */
static u64 task_pgd[MAX_TASKS][512] __attribute__((aligned(4096)));
static u64 task_l1[MAX_TASKS][512]  __attribute__((aligned(4096)));
static u64 task_l2[MAX_TASKS][512]  __attribute__((aligned(4096)));
static u64 task_l3[MAX_TASKS][512]  __attribute__((aligned(4096)));
u64 task_code_pa[MAX_TASKS], task_stack_pa[MAX_TASKS], task_ipc_pa[MAX_TASKS];

extern void vectors(void);
extern void el0_enter(u64 pc, u64 sp);
extern void kernel_after_user(void);

void mmu_init(void) {
    l0[0]   = (u64)l1a | T;
    l1a[0]  = (u64)l2a | T;       /* 0..1 GiB: device space */
    l1a[1]  = (u64)l2r | T;       /* 1..2 GiB: kernel RAM   */
    l2r[0]  = RAM_BASE | B | AF | SH | UXN;            /* 2 MiB: all RAM */
    l2a[72] = 0x09000000UL | B | (1UL<<2) | AF | PXN | UXN; /* UART */
    l2a[80] = 0x0a000000UL | B | (1UL<<2) | AF | PXN | UXN; /* virtio */
    l2a[2]  = (u64)l3u | T;       /* 4..6 MiB: reserved l3u window */

    __asm__ volatile("msr mair_el1,%0" :: "r"(0x04ffUL));
    __asm__ volatile("msr tcr_el1,%0"  :: "r"(16UL|(1UL<<8)|(1UL<<10)|(3UL<<12)));
    __asm__ volatile("msr ttbr0_el1,%0" :: "r"((u64)l0));
    __asm__ volatile("msr ttbr1_el1,%0" :: "r"((u64)l0));
    __asm__ volatile("msr vbar_el1,%0"  :: "r"((u64)vectors));
    __asm__ volatile(
        "dsb ish; isb;"
        "mrs x0,sctlr_el1; orr x0,x0,#1; orr x0,x0,#4; orr x0,x0,#0x1000;"
        "msr sctlr_el1,x0; isb"
        ::: "x0", "memory");
}

/* Build per-task EL0 page tables.
 * Allocates code/stack/IPC pages; queue/data/req pages are allocated and
 * mapped by kernel_main after calling mmu_task_init.
 * Returns the physical address of the task's L0 table (stored in task.pgd). */
u64 mmu_task_init(u32 id) {
    if (id >= MAX_TASKS) return 0;
    u64 *pgd = task_pgd[id], *l1 = task_l1[id];
    u64 *l2  = task_l2[id],  *l3 = task_l3[id];

    task_code_pa[id]  = (u64)page_alloc();
    task_stack_pa[id] = (u64)page_alloc();
    task_ipc_pa[id]   = (u64)page_alloc();

    pgd[0] = (u64)l1 | T;
    l1[0]  = (u64)l2 | T;         /* 0..1 GiB: task low window */
    l1[1]  = (u64)l2r | T;        /* 1..2 GiB: kernel RAM (EL1-only) */
    l2[2]  = (u64)l3 | T;         /* 2 MiB block covering all driver pages */

    l3[0]  = task_code_pa[id]  | P | AF | SH | U | PXN;   /* 0x400000 RX EL0 */
    l3[2]  = task_stack_pa[id] | P | AF | SH | U | UXN;   /* 0x402000 RW EL0 */
    l3[4]  = task_ipc_pa[id]   | P | AF | SH | U | UXN;   /* 0x404000 RW EL0 */
    /* L3[3]  = MMIO,          filled by mmu_map_page in kernel_main */
    /* L3[256..261] = queues/data/req, filled by mmu_map_page in kernel_main */
    return (u64)pgd;
}

/* Map a 4KB page into a task's tables.
 * pgd must be a task_pgd[id] pointer; the L0->L1->L2->L3 chain must exist.
 * After writing the L3 entry a TLB invalidation is issued. */
void mmu_map_page(u64 *pgd, u64 va, u64 pa, u64 attr) {
    u64 *l1 = (u64 *)(pgd[(va >> 39) & 511]  & ~0xFFFUL);
    u64 *l2 = (u64 *)(l1[(va >> 30)  & 511]  & ~0xFFFUL);
    u64 *l3 = (u64 *)(l2[(va >> 21)  & 511]  & ~0xFFFUL);
    l3[(va >> 12) & 511] = pa | attr;
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

/* Install a task's page table root as TTBR0_EL1.
 * Called from task_switch() in kernel.c which also loads SP/ELR/SPSR. */
void mmu_install_task(u64 pgd_pa) {
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd_pa));
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

/* Restore the kernel (shared) TTBR0 root. */
void mmu_restore_kernel(void) {
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((u64)l0));
    __asm__ volatile("tlbi vmalle1is; dsb ish; isb");
}

/* user_exception — entered from vectors.S el0_sync stub.
 * x0..x7 are EL0 argument registers; syscall is x8 (loaded by the stub). */
void user_exception(u64 x0,u64 x1,u64 x2,u64 x3,u64 x4,u64 x5,u64 x6,u64 x7,u64 syscall) {
    u64 esr, elr, far;
    __asm__ volatile("mrs %0,esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0,elr_el1" : "=r"(elr));
    __asm__ volatile("mrs %0,far_el1" : "=r"(far));
    (void)x0; (void)x4; (void)x5; (void)x6; (void)x7;
    /* Restore kernel tables before any EL1 data access. */
    mmu_restore_kernel();
    u64 ec = esr >> 26;

    if (ec == 0x15 && syscall == 4) {
        /* SVC #0 with x8=4: driver exit — return to kernel_after_user. */
        __asm__ volatile("msr elr_el1,  %0" :: "r"((u64)kernel_after_user));
        __asm__ volatile("msr spsr_el1, %0" :: "r"(0x3c5UL)); /* EL1h, DAIF masked */
        return;
    }
    if (ec == 0x15 && syscall == 5) { /* ipc_send(from,to,type,value) */
        /* Validate sender: must match the active task ID */
        u32 caller = task_current_id();
        ipc_send(caller, (u32)x1, (u32)x2, x3);
        mmu_install_task(task_current_pgd());
        return;
    }
    if (ec == 0x15 && syscall == 6) { /* ipc_recv(to,&from,&type,&value) */
        u32 caller = task_current_id();
        u32 from, type; u64 value;
        if (ipc_recv(caller, &from, &type, &value) == 0) {
            if (x1) *(u32*)x1 = from;
            if (x2) *(u32*)x2 = type;
            if (x3) *(u64*)x3 = value;
        }
        mmu_install_task(task_current_pgd());
        return;
    }

    /* Non-SVC synchronous exception or invalid syscall: FAULT REPORTING */
    u32 tid = task_current_id();
    uart_puts("mycelius: task ");
    { char num[2] = { '0' + (char)(tid % 10), 0 }; uart_puts(num); }
    uart_puts(" fault: ");
    if      (ec == 0x20 || ec == 0x21) uart_puts("instruction-abort");
    else if (ec == 0x24 || ec == 0x25) uart_puts("data-abort");
    else if (ec == 0x22)               uart_puts("pc-alignment");
    else if (ec == 0x26)               uart_puts("sp-alignment");
    else if (ec == 0x00)               uart_puts("undefined-instruction");
    else if (ec == 0x15)               uart_puts("unhandled-syscall");
    else                               uart_puts("unexpected-exception");

    uart_puts(" PC=0x");
    for (int i = 28; i >= 0; i -= 4) {
        u32 d = (u32)((elr >> i) & 0xf);
        char c[2] = { d < 10 ? '0' + d : 'a' + d - 10, 0 };
        uart_puts(c);
    }
    uart_puts(" FAR=0x");
    for (int i = 28; i >= 0; i -= 4) {
        u32 d = (u32)((far >> i) & 0xf);
        char c[2] = { d < 10 ? '0' + d : 'a' + d - 10, 0 };
        uart_puts(c);
    }
    uart_puts(" ESR=0x");
    for (int i = 28; i >= 0; i -= 4) {
        u32 d = (u32)((esr >> i) & 0xf);
        char c[2] = { d < 10 ? '0' + d : 'a' + d - 10, 0 };
        uart_puts(c);
    }
    uart_puts("\r\n");

    /* Mark the task faulted for supervision */
    task_fault(tid);

    /* Abort EL0 execution and safely return to EL1 kernel */
    __asm__ volatile("msr elr_el1,  %0" :: "r"((u64)kernel_after_user));
    __asm__ volatile("msr spsr_el1, %0" :: "r"(0x3c5UL));
}

void user_cli_start(void) { el0_enter(0x00400000UL, 0x00403000UL); }
