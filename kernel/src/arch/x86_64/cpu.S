.section .text

.global myos_halt_forever
.type myos_halt_forever, @function
myos_halt_forever:
    cli
1:
    hlt
    jmp 1b

.global myos_outb
.type myos_outb, @function
myos_outb:
    movw %di, %dx
    movb %sil, %al
    outb %al, %dx
    ret

.global myos_inb
.type myos_inb, @function
myos_inb:
    xorl %eax, %eax
    movw %di, %dx
    inb %dx, %al
    ret

.section .note.GNU-stack,"",@progbits

