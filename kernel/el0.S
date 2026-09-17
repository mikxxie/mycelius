.section .text
.global el0_enter
el0_enter:
    msr sp_el0, x1
    msr elr_el1, x0
    mov x0, #0
    msr spsr_el1, x0
    eret
