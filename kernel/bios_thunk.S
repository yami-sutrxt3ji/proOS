.intel_syntax noprefix

.section .text
.code32
.global bios_thunk_read
.type bios_thunk_read, @function
bios_thunk_read:
    mov eax, -1
    ret
