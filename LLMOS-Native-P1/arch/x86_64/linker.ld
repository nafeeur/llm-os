ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x0000000000010000;
    __kernel_start = .;
    .text : ALIGN(16) { KEEP(*(.text.boot)) *(.text .text.*) } :text
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) } :rodata
    .data : ALIGN(0x1000) { *(.data .data.*) } :data
    .bss (NOLOAD) : ALIGN(0x1000) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    } :data
    __kernel_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
