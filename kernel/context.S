.section .text
.code32

.global context_switch

/* void context_switch(struct context *old_ctx, struct context *new_ctx) */
context_switch:
    push %ebp
    mov %esp, %ebp

    pushfl
    push %eax
    push %ecx
    push %edx
    push %ebx
    push %esi
    push %edi

    mov 8(%ebp), %eax      /* old */
    mov %esp, (%eax)
    mov 12(%ebp), %eax     /* new */
    mov (%eax), %esp

    pop %edi
    pop %esi
    pop %ebx
    pop %edx
    pop %ecx
    pop %eax
    popfl
    pop %ebp
    ret
