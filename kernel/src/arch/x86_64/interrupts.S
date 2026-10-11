.section .text
.altmacro
.macro ISR vector
myos_isr_\vector:
    .if (\vector != 8) && (\vector != 10) && (\vector != 11) && (\vector != 12) && (\vector != 13) && (\vector != 14) && (\vector != 17) && (\vector != 21) && (\vector != 29) && (\vector != 30)
    pushq $0
    .endif
    pushq $\vector
    jmp myos_isr_common
.endm
.set vector, 0
.rept 256
ISR %vector
.set vector, vector + 1
.endr

myos_isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call myos_interrupt_dispatch
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.global myos_load_gdt
.type myos_load_gdt, @function
myos_load_gdt:
    lgdt (%rdi)
    pushq $8
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $16, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $24, %ax
    ltr %ax
    ret

.section .rodata
.global myos_isr_table
myos_isr_table:
.macro ISR_ADDRESS vector
    .quad myos_isr_\vector
.endm
.set vector, 0
.rept 256
ISR_ADDRESS %vector
.set vector, vector + 1
.endr
.section .note.GNU-stack,"",@progbits
