/* Private EL0 virtual base. Each task maps this VA to its own physical pages. */
ENTRY(app_main)
SECTIONS { . = 0x00400000; .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss (NOLOAD) : { *(.bss*) *(COMMON) } }
