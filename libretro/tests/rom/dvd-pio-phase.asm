; SPDX-License-Identifier: GPL-2.0-or-later
; READ(10): consume whole multi-sector PIO phases, including a sector tail.
%ifndef BCL
%define BCL 65534
%endif
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
    mov esi,read10
    call packet
    mov edi,0x400000
    mov ebx,137*2048
.phase:
    call wait_ready
    test al,1
    jnz fail
    test al,8
    jz fail
%ifdef PIO_DELAY
    ; Private savestate runs can leave a complete phase exposed across frames.
    mov ecx,PIO_DELAY
.delay:
    dec ecx
    jnz .delay
%endif
    mov dx,0x1f4
    in al,dx
    mov cl,al
    inc dx
    in al,dx
    mov ch,al
    movzx ecx,cx
    test ecx,ecx
    jz fail
    test ecx,1
    jnz fail
    cmp ecx,ebx
    ja fail
    sub ebx,ecx
    shr ecx,1
    mov dx,0x1f0
    rep insw                  ; the entire announced phase, no status polls
    inc dword [R+20]
    test ebx,ebx
    jnz .phase
    call wait_ready
    test al,0x89
    jnz fail
    mov ebp,4
    mov esi,0x400000
    mov ecx,137*512
    mov ebx,0x10000000+121*512
.verify:
    lodsd
    cmp eax,ebx
    jne .bad
    inc ebx
    inc dword [R+12]
    loop .verify
    mov dword [R],0x4b4f5044
    jmp .halt
.bad:
    mov [R+16],esi
    jmp fail
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
    mov al,BCL & 255
    out dx,al
    inc dx
    mov al,BCL >> 8
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
read10: db 0x28,0,0,0,0,121,0,0,137,0,0,0
times 0x100000-($-$$) db 0xff
