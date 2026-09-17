.section .text
.global mycelius_send
.global mycelius_receive
/* ABI: x8 selects a kernel service; x0/x1 are ordinary function arguments. */
mycelius_send:
    mov x8, #1
    svc #0
    ret
mycelius_receive:
    mov x8, #2
    svc #0
    ret
