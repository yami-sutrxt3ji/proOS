.section .text
.code32

.global isr128

.extern syscall_handler

isr128:
    pushl $0
    pushl $0x80
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    mov %esp, %eax
    pushl %eax
    call syscall_handler
    add $4, %esp

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    add $8, %esp
    iret
