#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 1
#endif

.section .text
.code16
.global boot_start
boot_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    sti
    movb %dl, boot_drive

    movw $0x55aa, %bx
    movb $0x41, %ah
    movb boot_drive, %dl
    int $0x13
    jc disk_error
    cmpw $0xaa55, %bx
    jne disk_error
    testw $0x0001, %cx
    jz disk_error

    /* A single INT 13h AH=42 transfer is bound to one real-mode segment
     * (64 KiB); KERNEL_SECTORS*512 has exceeded that, so read in chunks of
     * at most 64 sectors (32 KiB) each, advancing the destination segment
     * and LBA between calls instead of raising the per-call size. */
    xorw %ax, %ax
    movw %ax, %ds
    movw $KERNEL_SECTORS, %cx
    movw $0x1000, %di
read_loop:
    testw %cx, %cx
    jz read_done
    movw %cx, %bx
    cmpw $64, %bx
    jbe 1f
    movw $64, %bx
1:
    movw %bx, disk_packet_count
    movw %di, disk_packet_seg
    movw $disk_packet, %si
    movb $0x42, %ah
    movb boot_drive, %dl
    int $0x13
    jc disk_error
    subw %bx, %cx
    movw %bx, %dx
    shlw $5, %dx
    addw %dx, %di
    addw %bx, disk_packet_lba_lo
    adcw $0, disk_packet_lba_lo+2
    jmp read_loop
read_done:

    cli
    inb $0x92, %al
    orb $0x02, %al
    andb $0xfe, %al
    outb %al, $0x92
    lgdt gdt_descriptor
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_mode

disk_error:
    movw $0x3f8, %dx
    movb $0x45, %al
    outb %al, %dx
1:
    cli
    hlt
    jmp 1b

.code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x00090000, %esp

    xorl %eax, %eax
    movl $0x1000, %edi
    movl $(0x3000 / 4), %ecx
    rep stosl

    movl $(0x2000 | 0x003), 0x1000
    movl $0, 0x1004
    movl $(0x3000 | 0x003), 0x2000
    movl $0, 0x2004
    movl $0x3000, %edi
    movl $0x00000083, %ebx
    movl $512, %ecx
2:
    movl %ebx, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %ebx
    addl $8, %edi
    loop 2b

    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4
    movl $0x1000, %eax
    movl %eax, %cr3
    movl $0xc0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr
    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0
    ljmpl $0x18, $long_mode

.code64
long_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movq $0x02000000, %rsp
    xorq %rbp, %rbp
    movabsq $0x0000000000010000, %rax
    jmp *%rax

.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.align 4
boot_drive:
    .byte 0

.align 4
disk_packet:
    .byte 0x10
    .byte 0x00
disk_packet_count:
    .word 0
disk_packet_off:
    .word 0x0000
disk_packet_seg:
    .word 0x1000
disk_packet_lba_lo:
    .long 1
disk_packet_lba_hi:
    .long 0

.org 510
.word 0xaa55
.section .note.GNU-stack,"",@progbits
