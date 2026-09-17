.section .text
.align 11
.global vectors
.extern user_exception
vectors:
    .rept 8
    b .; .space 124
    .endr
    sub sp, sp, #32
    stp x0, x8, [sp]
    mov x1, x8
    bl user_exception
    ldp x0, x8, [sp]
    add sp, sp, #32
    eret
    .space 124
    .rept 7
    b .; .space 124
    .endr
