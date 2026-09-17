#include "kernel.h"
#define T 3UL
#define B 1UL
#define P 3UL
#define AF (1UL << 10)
#define SH (3UL << 8)
#define U (1UL << 6)
#define PXN (1UL << 53)
#define UXN (1UL << 54)
static u64 l0[512] __attribute__((aligned(4096))), l1a[512] __attribute__((aligned(4096))), l2a[512] __attribute__((aligned(4096))), l2r[512] __attribute__((aligned(4096))), l3u[512] __attribute__((aligned(4096)));
static u8 code[4096] __attribute__((aligned(4096))), stack[4096] __attribute__((aligned(4096)));
extern void vectors(void); extern void el0_enter(u64,u64);
void mmu_init(void) {
    l0[0]=(u64)l1a|T; l1a[0]=(u64)l2a|T; l1a[1]=(u64)l2r|T;
    l2r[0]=RAM_BASE|B|AF|SH|UXN; l2a[72]=0x09000000UL|B|(1UL<<2)|AF|PXN|UXN; l2a[80]=0x0a000000UL|B|(1UL<<2)|AF|PXN|UXN; l2a[2]=(u64)l3u|T;
    l3u[0]=(u64)code|P|AF|SH|U|PXN; l3u[2]=(u64)stack|P|AF|SH|U|PXN|UXN;
    /* An EL0 payload: SVC write(3), then SVC exit(4); no direct UART access. */
    const u32 app[]={0xd2800068,0x100000a0,0xd4000001,0xd2800088,0xd4000001,0x14000000,0x6c6c6568,0x7266206f,0x69206d6f,0x616c6f73,0x20646574,0x304c4545,0x696c6320,0x0a0d0000};
    for(u32 i=0;i<sizeof app/4;i++) ((u32*)code)[i]=app[i];
    __asm__ volatile("msr mair_el1,%0"::"r"(0x04ffUL));
    __asm__ volatile("msr tcr_el1,%0"::"r"(16UL|(1UL<<8)|(1UL<<10)|(3UL<<12)));
    __asm__ volatile("msr ttbr0_el1,%0"::"r"((u64)l0)); __asm__ volatile("msr vbar_el1,%0"::"r"((u64)vectors));
    __asm__ volatile("dsb ish; isb; mrs x0,sctlr_el1; orr x0,x0,#1; orr x0,x0,#4; orr x0,x0,#0x1000; msr sctlr_el1,x0; isb":::"x0","memory");
}
void user_cli_start(void) { el0_enter(0x00400000,0x00403000); }
void user_exception(u64 x0,u64 syscall) { u64 esr; __asm__ volatile("mrs %0,esr_el1":"=r"(esr)); if((esr>>26)==0x15 && syscall==3) uart_puts((const char*)x0); if((esr>>26)==0x15 && syscall==4) { __asm__ volatile("msr elr_el1,%0"::"r"((u64)kernel_after_user)); __asm__ volatile("msr spsr_el1,%0"::"r"(0x3c5UL)); } }
