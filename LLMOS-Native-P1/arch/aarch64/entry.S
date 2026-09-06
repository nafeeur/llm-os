.section .text.boot,"ax"
.global _start
.extern kernel_main
.extern __bss_start
.extern __bss_end
.extern __stack_top

_start:
    mov x19, x0
    mrs x1, mpidr_el1
    and x1, x1, #0xff
    cbz x1, 1f
0:
    wfe
    b 0b
1:
    msr daifset, #0xf
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #2
    b.ne 2f

    mov x2, #0x80000000
    msr hcr_el2, x2
    mov x2, #0x3
    msr cnthctl_el2, x2
    msr cntvoff_el2, xzr
    adr x2, 2f
    msr elr_el2, x2
    mov x2, #0x3c5
    msr spsr_el2, x2
    ldr x2, =__stack_top
    msr sp_el1, x2
    eret

2:
    ldr x2, =__stack_top
    mov sp, x2
    mrs x2, cpacr_el1
    orr x2, x2, #(3 << 20)
    msr cpacr_el1, x2
    isb

    ldr x1, =__bss_start
    ldr x2, =__bss_end
3:
    cmp x1, x2
    b.hs 4f
    str xzr, [x1], #8
    b 3b
4:
    mov x0, x19
    bl kernel_main
5:
    wfe
    b 5b
.section .note.GNU-stack,"",@progbits
