.section .text.boot,"ax"
.code64
.global _start
.extern kernel_main
.extern __bss_start
.extern __bss_end
_start:
    cli
    cld
    leaq __bss_start(%rip), %rdi
    leaq __bss_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb
    xorq %rdi, %rdi
    call kernel_main
1:
    cli
    hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
