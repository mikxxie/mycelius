.section .text
.align 11
.global vectors
.extern user_exception

/* Each vector slot is 0x80 bytes. SVC from EL0 is a lower-EL AArch64
 * synchronous exception, so it enters at VBAR+0x400, not at offset 0. */
vectors:
    /* 0x000: current EL, SP_EL0 */
    b .; .space 124
    b .; .space 124
    b .; .space 124
    b .; .space 124
    /* 0x200: current EL, SP_ELx */
    b .; .space 124
    b .; .space 124
    b .; .space 124
    b .; .space 124
    /* 0x400: lower EL, AArch64 */
    b el0_sync; .space 124
    b .; .space 124
    b .; .space 124
    b .; .space 124
    /* 0x600: lower EL, AArch32 */
    b .; .space 124
    b .; .space 124
    b .; .space 124
    b .; .space 124

/* x0-x7 are the syscall arguments. AAPCS64 has no ninth register argument,
 * so the syscall number (x8) is passed on the stack. Caller-saved registers
 * are preserved around the C handler so ERET resumes the EL0 task. */
el0_sync:
    sub sp, sp, #176
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    stp x8, x9, [sp, #80]
    stp x10, x11, [sp, #96]
    stp x12, x13, [sp, #112]
    stp x14, x15, [sp, #128]
    stp x16, x17, [sp, #144]
    stp x18, x30, [sp, #160]
    str x8, [sp]
    bl user_exception
    ldp x0, x1, [sp, #16]
    ldp x2, x3, [sp, #32]
    ldp x4, x5, [sp, #48]
    ldp x6, x7, [sp, #64]
    ldp x8, x9, [sp, #80]
    ldp x10, x11, [sp, #96]
    ldp x12, x13, [sp, #112]
    ldp x14, x15, [sp, #128]
    ldp x16, x17, [sp, #144]
    ldp x18, x30, [sp, #160]
    add sp, sp, #176
    eret
