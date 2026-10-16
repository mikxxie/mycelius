.section .text
.align 11
.global vectors
.extern user_exception
vectors:
    b el1_sync          // 0x000: sync from EL1 (SVC from driver)
    b .                 // 0x080: IRQ EL1 SP0
    b .                 // 0x100: FIQ EL1 SP0
    b .                 // 0x180: SError EL1 SP0
    b el1_sync          // 0x200: sync from lower EL (EL0) -> handler
    b .                 // 0x280: IRQ from lower EL
    b .                 // 0x300: FIQ from lower EL
    b .                 // 0x380: SError from lower EL
el1_sync:               // 0x400: handler entry
    sub sp, sp, #64
    stp x0, x1, [sp]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    bl user_exception      // x0-x7 preserved, x8 = syscall number
    ldp x0, x1, [sp]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    add sp, sp, #64
    eret
    .space 124
    .rept 7
    b .; .space 124
    .endr
