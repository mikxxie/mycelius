#include "kernel.h"
#define T 3UL
#define B 1UL
#define P 3UL
#define AF (1UL << 10)
#define SH (3UL << 8)
#define U (1UL << 6)
#define PXN (1UL << 53)
#define UXN (1UL << 54)
u64 l0[512] __attribute__((aligned(4096))), l1a[512] __attribute__((aligned(4096))), l2a[512] __attribute__((aligned(4096))), l2r[512] __attribute__((aligned(4096))), l3u[512] __attribute__((aligned(4096)));
static u8 code[4096] __attribute__((aligned(4096))), stack[4096] __attribute__((aligned(4096)));
extern void vectors(void); extern void el0_enter(u64,u64);
/* Per-task EL0 translation tables. Each task maps only its code, stack, IPC
 * page and assigned MMIO window, plus kernel RAM needed for exception
 * handling. */
static u64 task_pgd[MAX_TASKS][512] __attribute__((aligned(4096)));
static u64 task_l1[MAX_TASKS][512] __attribute__((aligned(4096)));
static u64 task_l2[MAX_TASKS][512] __attribute__((aligned(4096)));
static u64 task_l3[MAX_TASKS][512] __attribute__((aligned(4096)));
u64 task_code_pa[MAX_TASKS], task_stack_pa[MAX_TASKS], task_ipc_pa[MAX_TASKS];
void mmu_init(void) {
    l0[0]=(u64)l1a|T; l1a[0]=(u64)l2a|T; l1a[1]=(u64)l2r|T;
    l2r[0]=RAM_BASE|B|AF|SH|UXN; l2a[72]=0x09000000UL|B|(1UL<<2)|AF|PXN|UXN; l2a[80]=0x0a000000UL|B|(1UL<<2)|AF|PXN|UXN; l2a[2]=(u64)l3u|T;
    l3u[0]=(u64)code|P|AF|SH|U|PXN; l3u[2]=(u64)stack|P|AF|SH|U|PXN|UXN;
    /* An EL0 payload: SVC write(3), then SVC exit(4); no direct UART access. */
    const u32 app[]={0xd2800068,0x100000a0,0xd4000001,0xd2800088,0xd4000001,0x14000000,0x6c6c6568,0x7266206f,0x69206d6f,0x616c6f73,0x20646574,0x304c4545,0x696c6320,0x0a0d0000};
    for(u32 i=0;i<sizeof app/4;i++) ((u32*)code)[i]=app[i];
    __asm__ volatile("msr mair_el1,%0"::"r"(0x04ffUL));
    __asm__ volatile("msr tcr_el1,%0"::"r"(16UL|(1UL<<8)|(1UL<<10)|(3UL<<12)));
    __asm__ volatile("msr ttbr0_el1,%0"::"r"((u64)l0));
    __asm__ volatile("msr ttbr1_el1,%0"::"r"((u64)l0));
    __asm__ volatile("msr vbar_el1,%0"::"r"((u64)vectors));
    __asm__ volatile("dsb ish; isb; mrs x0,sctlr_el1; orr x0,x0,#1; orr x0,x0,#4; orr x0,x0,#0x1000; msr sctlr_el1,x0; isb":::"x0","memory");
}
/* Build per-task EL0 translation tables.  EL0 gets only code, IPC, stack and
 * its MMIO page; the kernel RAM mapping is EL1-only and exists for exceptions.
 * Returns the physical address of the task's L0 page table. */
u64 mmu_task_init(u32 id) {
    if (id >= MAX_TASKS) return 0;
    u64 *pgd = task_pgd[id], *l1 = task_l1[id], *l2 = task_l2[id];
    u64 *l3 = task_l3[id];
    task_code_pa[id] = (u64)page_alloc();
    task_stack_pa[id] = (u64)page_alloc();
    task_ipc_pa[id]  = (u64)page_alloc();
    pgd[0] = (u64)l1 | T;
    l1[0] = (u64)l2 | T;            /* task L2 covers [0, 1GB) */
    l1[1] = (u64)l2r | T;           /* kernel RAM at [1GB, 2GB) for exceptions */
    l2[2] = (u64)l3 | T; /* TASK_* addresses live in the 4 MiB region. */
    l3[0] = task_code_pa[id] | P | AF | SH | U | PXN;   /* 0x00400000: EL0 RX */
    l3[2] = task_stack_pa[id] | P | AF | SH | U | UXN;  /* 0x00402000: EL0 RW */
    l3[4] = task_ipc_pa[id] | P | AF | SH | U | UXN;    /* 0x00404000: EL0 RW */
    return (u64)pgd;
}
/* Map a 4KB page at VA into the given page table. Walks L0->L1->L2->L3. */
void mmu_map_page(u64 *pgd, u64 va, u64 pa, u64 attr) {
    u64 *l1 = (u64 *)(pgd[0] & ~0xFFF);
    u64 *l2 = (u64 *)(l1[(va >> 30) & 511] & ~0xFFF);
    u64 *l3 = (u64 *)(l2[(va >> 21) & 511] & ~0xFFF);
    l3[(va >> 12) & 511] = pa | attr;
}
void user_cli_start(void) { el0_enter(0x00400000,0x00403000); }
void user_exception(u64 x0,u64 x1,u64 x2,u64 x3,u64 x4,u64 x5,u64 x6,u64 x7,u64 syscall) {
    u64 esr;
    __asm__ volatile("mrs %0,esr_el1":"=r"(esr));
    (void)x4; (void)x5; (void)x6; (void)x7;
    /* Switch back to kernel page table before any kernel access */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((u64)l0));
    __asm__ volatile("tlbi vmalle1is; dsb ish; isb");
    u64 ec = esr >> 26;
    uart_puts("mycelius: EL0 exception esr=");
    if (ec == 0x15) uart_puts("SVC ");
    else if (ec == 0x20) uart_puts("instruction-abort ");
    else if (ec == 0x24) uart_puts("data-abort ");
    else uart_puts("unexpected ");
    uart_puts(" syscall=");
    if(syscall==3) uart_puts("3(write)");
    if(syscall==4) uart_puts("4(exit)");
    if(syscall==5) uart_puts("5(ipc_send)");
    if(syscall==6) uart_puts("6(ipc_recv)");
    uart_puts("\r\n");
    if (ec == 0x15 && syscall == 4) {
        /* Driver done: switch back to kernel TTBR0 and return to EL1. */
        __asm__ volatile("msr elr_el1, %0" :: "r"((u64)kernel_after_user));
        __asm__ volatile("msr spsr_el1, %0" :: "r"(0x3c5UL));  /* EL1h, DAIF masked */
    }
    if (ec == 0x15 && syscall == 5) { /* ipc_send(from,to,type,value) */
        ipc_send((u32)x0,(u32)x1,(u32)x2,x3);
    }
    if (ec == 0x15 && syscall == 6) { /* ipc_recv(to,&from,&type,&value) */
        u32 from,type; u64 value;
        if(ipc_recv((u32)x0,&from,&type,&value)==0) {
            if(x1) *(u32*)x1 = from;
            if(x2) *(u32*)x2 = type;
            if(x3) *(u64*)x3 = value;
        }
    }
    if (ec != 0x15 || (syscall != 5 && syscall != 6)) {
        /* Never ERET back to EL0 with the kernel page table after a fault or
         * unknown service.  Treat it as a terminated bring-up driver. */
        __asm__ volatile("msr elr_el1, %0" :: "r"((u64)kernel_after_user));
        __asm__ volatile("msr spsr_el1, %0" :: "r"(0x3c5UL));
    }
}
