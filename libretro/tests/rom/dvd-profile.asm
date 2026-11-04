; SPDX-License-Identifier: GPL-2.0-or-later
; GET CONFIGURATION reports DVD-ROM for the small Xbox test image.
bits 32
org 0xfff00000
%include "rep-common.inc"
start:
    rep_test_init
    mov dx,0xcf8
    mov eax,0x80004804 ; IDE controller, device9, command register
    out dx,eax
    mov dx,0xcfc
    mov eax,1
    out dx,eax
    mov dx,0x1f6
    mov al,0xb0 ; primary slave
    out dx,al
    mov ebp,2
    mov ecx,8
.ready:
    mov esi,tur
    call packet
    call wait_ready
    test al,1
    jz .configured
    loop .ready
    jmp fail
.configured:
    mov ebp,3
    mov esi,configuration
    call packet
    call wait_ready
    test al,1
    jnz fail
    test al,8
    jz fail
    mov ebp,4
    mov dx,0x1f4
    in al,dx
    mov bl,al
    inc dx
    in al,dx
    mov bh,al
    movzx ecx,bx
    mov [R+12],ecx
    cmp ecx,8
    jb fail
    cmp ecx,32
    ja fail
    test ecx,1
    jnz fail
    shr ecx,1
    mov dx,0x1f0
    mov edi,0x301000
    rep insw
    call wait_ready
    test al,0x81
    jnz fail
    movzx eax,word [0x301006]
    rol ax,8
    mov [R+8],eax
    check eax,0x10
    mov dword [R],0x4b4f5044 ; DPOK
.halt:
    cli
    hlt
    jmp .halt
wait_ready:
    push ecx
    mov ecx,100000
    mov dx,0x3f6
.poll:
    in al,dx
    test al,0x80
    jz .done
    loop .poll
    pop ecx
    jmp fail
.done:
    pop ecx
    ret
packet:
    push ecx
    call wait_ready
    mov dx,0x1f1
    xor al,al
    out dx,al
    mov dx,0x1f4
    mov al,0xfe
    out dx,al
    inc dx
    mov al,0xff
    out dx,al
    mov dx,0x1f7
    mov al,0xa0
    out dx,al
    call wait_ready
    test al,8
    jz fail
    mov ecx,6
    mov dx,0x1f0
    rep outsw
    pop ecx
    ret
rep_test_handlers
tur: times 12 db 0
configuration: db 0x46,0,0,0,0,0,0,0,32,0,0,0
times 0x100000-($-$$) db 0xff
