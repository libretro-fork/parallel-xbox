; SPDX-License-Identifier: GPL-2.0-or-later
; Enough distinct RAM translations to repeatedly exhaust a 1 MiB test cache.
bits 32
org 0xfff00000
%include "rep-common.inc"
start:
    rep_test_init
    mov edi,0x400000
    mov ecx,20000
.fill:
    mov word [edi],0x05ff ; inc dword [R+0x10]
    mov dword [edi+2],R+0x10
    mov byte [edi+6],0xe9 ; jmp to the next 16-byte block
    mov dword [edi+7],5
    mov dword [edi+11],0x90909090
    mov byte [edi+15],0x90
    add edi,16
    loop .fill
    mov byte [edi],0xc3
    clc
    call 0x400000
    check dword [R+0x10],20000
    inc ebp
    call 0x400000
    check dword [R+0x10],40000
    mov dword [R],0x4b4f4254 ; TBOK
.halt:
    cli
    hlt
    jmp .halt
    rep_test_handlers
times 0x100000-($-$$) db 0xff
