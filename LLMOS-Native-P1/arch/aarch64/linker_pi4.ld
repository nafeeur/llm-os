ENTRY(_start)
SECTIONS {
    . = 0x00080000;
    __kernel_start = .;
    .text : ALIGN(16) { KEEP(*(.text.boot)) *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .data : ALIGN(0x1000) { *(.data .data.*) }
    .bss (NOLOAD) : ALIGN(0x1000) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
        . = ALIGN(16);
        __stack_bottom = .;
        . += 0x20000;
        __stack_top = .;
    }
    __kernel_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
