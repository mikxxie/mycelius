/* QEMU virt RAM starts at 0x40000000.  Leave its low area for firmware/DTB. */
ENTRY(_start)
SECTIONS {
  . = 0x40080000;
  .text : { KEEP(*(.text.boot)) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss (NOLOAD) : {
    . = ALIGN(16); __bss_start = .; *(.bss*) *(COMMON); __bss_end = .;
    . = ALIGN(16); __stack_bottom = .; . += 8192; __stack_top = .;
  }
  /DISCARD/ : { *(.comment*) *(.eh_frame*) *(.note*) }
}
