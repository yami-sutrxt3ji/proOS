; proOS Stage 1 Bootloader (MBR)
; Loads the stage 2 loader from disk (sectors 2-5) into memory at 0x7E00.
; Assumes BIOS provides boot drive in DL.

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    call load_stage2
    jc disk_error

    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFFSET

; Attempt to use INT 13h extensions; fall back to CHS if unavailable.
load_stage2:
    push ax
    push bx
    push cx
    push dx
    push si

    mov dl, [boot_drive]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .chs
    cmp bx, 0xAA55
    jne .chs
    test cx, 0x0001
    jz .chs

    mov word [dap_sector_count], STAGE2_SECTORS
    mov word [dap_buffer_offset], STAGE2_LOAD_OFFSET
    mov word [dap_buffer_segment], STAGE2_LOAD_SEG
    mov dword [dap_lba_low], STAGE2_START_LBA
    mov dword [dap_lba_high], 0

    mov si, dap_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .done

.chs:
    mov bx, STAGE2_LOAD_OFFSET
    mov ax, STAGE2_LOAD_SEG
    mov es, ax

    mov ah, 0x02
    mov al, STAGE2_SECTORS
    mov ch, 0x00
    mov cl, 0x02
    mov dh, 0x00
    mov dl, [boot_drive]
    int 0x13
    jc .error

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

.error:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

disk_error:
    mov si, disk_error_msg
print_loop:
    lodsb
    or al, al
    jz halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_loop

halt:
    cli
    hlt
    jmp halt

boot_drive: db 0

dap_packet:
    db 0x10
    db 0x00
dap_sector_count: dw 0
dap_buffer_offset: dw 0
dap_buffer_segment: dw 0
dap_lba_low: dd 0
dap_lba_high: dd 0

disk_error_msg: db "Disk read error", 0

STAGE2_LOAD_SEG    EQU 0x0000
STAGE2_LOAD_OFFSET EQU 0x7E00
STAGE2_SECTORS     EQU 4
STAGE2_START_LBA   EQU 1

TIMES 510-($-$$) db 0
dw 0xAA55
