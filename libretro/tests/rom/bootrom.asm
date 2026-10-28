; Test boot ROM (512 bytes at 0xFFFFFE00): protected mode, flat segments,
; then the flash program at 0xFFF00000. Stands in for the MCPX ROM in the
; core's tests; not an Xbox boot ROM.
        bits 16
        org 0xFE00
start16:
        cli
        cld
        o32 lgdt [cs:gdtr]
        mov eax, cr0
        or al, 1
        mov cr0, eax
        jmp dword 0x08:(0xFFFF0000 + pm32)
        bits 32
pm32:
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov fs, ax
        mov gs, ax
        mov esp, 0x00090000
        mov eax, 0xFFF00000
        jmp eax
        align 8
gdt:    dq 0
        dq 0x00CF9A000000FFFF
        dq 0x00CF92000000FFFF
gdtr:   dw 23
        dd 0xFFFF0000 + gdt
        times 0x1F0 - ($ - $$) db 0x90
        bits 16
        jmp start16
        times 512 - ($ - $$) db 0xF4
