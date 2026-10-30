; Independent synthetic 2BL: check HLE's CPU/GDT handoff and ROM unmapping.
bits 32
org 0x90000
dd entry
entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    jmp 0x08:flat
flat:
    mov esp, 0x80000
    push dword 0x12345678
    pop eax
    cmp eax, 0x12345678
    jne failed
    mov eax, cr0
    test al, 1
    jz failed
    mov dword [0x1000], 0x484c4531
    inc dword [0x100c]
    mov dx, 0xcf8
    mov eax, 0x80000880
    out dx, eax
    mov dx, 0xcfc
    mov al, 2
    out dx, al
    mov eax, [0xfffffe08]
    mov [0x1008], eax
    jmp $
failed:
    mov dword [0x1000], 0xdeadbeef
    jmp $
times 0x5fe4 - ($ - $$) db 0
dd 0x7854794a
times 0x6000 - ($ - $$) db 0
