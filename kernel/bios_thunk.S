.intel_syntax noprefix

.equ THUNK_PARAM_BASE,       0x00000520
.equ THUNK_FUNC,             THUNK_PARAM_BASE + 0
.equ THUNK_DRIVE,            THUNK_PARAM_BASE + 1
.equ THUNK_STATUS,           THUNK_PARAM_BASE + 2
.equ THUNK_ERROR,            THUNK_PARAM_BASE + 4
.equ THUNK_DAP_SEG,          THUNK_PARAM_BASE + 6
.equ THUNK_DAP_OFF,          THUNK_PARAM_BASE + 8
.equ THUNK_SAVED_CR0,        THUNK_PARAM_BASE + 12
.equ THUNK_SAVED_SP,         THUNK_PARAM_BASE + 16

.equ REALMODE_STACK_SEG,     0x0000
.equ REALMODE_STACK_PTR,     0x8000

.section .text
.code32

.global bios_thunk_read
.type bios_thunk_read, @function
bios_thunk_read:
    push ebp
    mov ebp, esp
    push dword ptr [ebp + 16]
    push dword ptr [ebp + 12]
    push dword ptr [ebp + 8]
    push 0x42
    call bios_thunk_common
    add esp, 16
    leave
    ret

.global bios_thunk_write
.type bios_thunk_write, @function
bios_thunk_write:
    push ebp
    mov ebp, esp
    push dword ptr [ebp + 16]
    push dword ptr [ebp + 12]
    push dword ptr [ebp + 8]
    push 0x43
    call bios_thunk_common
    add esp, 16
    leave
    ret

.type bios_thunk_common, @function
bios_thunk_common:
    push ebp
    mov ebp, esp
    pushfd
    pushad

    mov eax, dword ptr [ebp + 8]
    mov byte ptr [THUNK_FUNC], al

    mov eax, dword ptr [ebp + 12]
    mov byte ptr [THUNK_DRIVE], al

    mov eax, dword ptr [ebp + 16]
    mov word ptr [THUNK_DAP_SEG], ax

    mov eax, dword ptr [ebp + 20]
    mov word ptr [THUNK_DAP_OFF], ax

    mov word ptr [THUNK_STATUS], 0xFFFF
    mov word ptr [THUNK_ERROR], 0

    cli

    mov eax, cr0
    mov dword ptr [THUNK_SAVED_CR0], eax
    mov dword ptr [THUNK_SAVED_SP], esp

    and eax, 0xFFFFFFFE
    mov cr0, eax
    ljmp 0x0000, real_mode_entry

.code32
pm_resume:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, dword ptr [THUNK_SAVED_SP]

    cmp word ptr [THUNK_STATUS], 0
    jne .pm_error
    xor eax, eax
    jmp .pm_done

.pm_error:
    mov eax, -1

.pm_done:
    popad
    popfd
    leave
    ret

.section .text
.code16
real_mode_entry:
    cli
    mov ax, REALMODE_STACK_SEG
    mov ss, ax
    mov sp, REALMODE_STACK_PTR

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov dl, byte ptr [THUNK_DRIVE]
    mov ah, byte ptr [THUNK_FUNC]
    mov bx, word ptr [THUNK_DAP_SEG]
    mov si, word ptr [THUNK_DAP_OFF]
    mov ds, bx

    int 0x13

    mov dh, ah
    pushf
    pop ax
    mov cx, ax

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    test cx, 1
    jz .thunk_success

    mov word ptr [THUNK_STATUS], 1
    mov byte ptr [THUNK_ERROR], dh
    jmp .thunk_done

.thunk_success:
    mov word ptr [THUNK_STATUS], 0

.thunk_done:
    mov ax, REALMODE_STACK_SEG
    mov ss, ax
    mov sp, REALMODE_STACK_PTR

    mov eax, dword ptr [THUNK_SAVED_CR0]
    or eax, 1
    mov cr0, eax
    .byte 0x66, 0xEA
    .long pm_resume
    .word 0x0008

.code32
