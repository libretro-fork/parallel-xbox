; SPDX-License-Identifier: GPL-2.0-or-later
; Shared flat protected-mode REP test setup and exception handlers.
%define R 0x300000
%macro check 2
    cmp %1,%2
    jne fail
%endmacro
%macro rep_test_init 0
    cli
    cld
    mov edi,R
    xor eax,eax
    mov ecx,16
    rep stosd
    mov ebp,1
    ; Install #DB, #PF and IRQ0 gates in an otherwise empty IDT.
    mov edi,0x7000
    mov ecx,512
    rep stosd
    mov edi,0x7008
    mov eax,debug_handler
    call gate
    mov edi,0x7070
    mov eax,fault_handler
    call gate
    mov edi,0x7100
    mov eax,irq_handler
    call gate
    lidt [idtr]
%endmacro

%macro rep_test_handlers 0
fail:
    mov [R+4],ebp
    jmp start.halt
gate:
    mov word [edi],ax
    mov word [edi+2],8
    mov word [edi+4],0x8e00
    shr eax,16
    mov word [edi+6],ax
    ret
debug_handler:
    push eax
    mov eax,dr6
    test eax,2
    jnz .data
    test eax,1
    jnz .hardware
    inc dword [R+8]
    test ecx,ecx
    jnz .return
    and dword [esp+12],~0x100
    jmp .return
.hardware:
    inc dword [R+12]
    xor eax,eax
    mov dr7,eax
    jmp .return
.data:
    inc dword [R+32]
    mov [R+36],ecx
    xor eax,eax
    mov dr7,eax
.return:
    xor eax,eax
    mov dr6,eax
    pop eax
    iretd
irq_handler:
    push eax
    cmp dword [R+16],0
    jne .ack
    mov [R+20],ecx
.ack:
    inc dword [R+16]
    mov al,0x20
    out 0x20,al
    pop eax
    iretd
fault_handler:
    inc dword [R+24]
    mov [R+28],ecx
    mov dword [0x500010],0x1000083
    invlpg [0x1000000]
    add esp,4
    iretd
idtr:
    dw 2047
    dd 0x7000
%endmacro
