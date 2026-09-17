.section .text.boot
.global _start
.extern kernel_main
.extern __bss_start
.extern __bss_end
.extern __stack_top

_start:
    /* QEMU -kernel commonly enters EL1; firmware-style launchers use EL2. */
    mrs x0, CurrentEL
    cmp x0, #0x4
    b.eq 1f                         /* already EL1 */
    cmp x0, #0x8
    b.ne hang                        /* this sample expects EL2 or EL1 */
    ldr x0, =__stack_top
    msr sp_el1, x0
    mov x0, #0x3c5                  /* EL1h, DAIF masked */
    msr spsr_el2, x0
    adr x0, 1f
    msr elr_el2, x0
    mov x0, #0x80000000
    msr hcr_el2, x0                 /* HCR.RW: EL1 executes AArch64 */
    isb
    eret
1:  ldr x0, =__stack_top
    mov sp, x0
    /* Clear only our statically linked BSS; no runtime or boot allocator. */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
2:  cmp x0, x1
    b.hs 3f
    str xzr, [x0], #8
    b 2b
3:  bl kernel_main
hang: wfe
    b hang
