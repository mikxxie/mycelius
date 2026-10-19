.section .text
.align 11
.global vectors
.extern user_exception
vectors:
    b el0_sync          // 0x000: Current EL0 sync (SVC, etc.)
    b .                 // 0x080: Current EL0 IRQ
    b .                 // 0x100: Current EL0 FIQ
    b .                 // 0x180: Current EL0 SError
    b .                 // 0x200: EL1 sync
    b .                 // 0x280: EL1 IRQ
    b .                 // 0x300: EL1 FIQ
    b .                 // 0x380: EL1 SError
el0_sync:
    /* Save x0-x7 */
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    /* x8 already has syscall number */
    bl user_exception
    /* Restore registers */
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    eret
