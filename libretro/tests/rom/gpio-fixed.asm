; SPDX-License-Identifier: GPL-2.0-or-later
; GPIO is decoded before PM BAR enable, and stays at 0x80c0 when it moves.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro config 2
    mov dx,0xcf8
    mov eax,0x80000800 | %1 ; LPC, bus 0 device 1 function 0
    out dx,eax
    mov dx,0xcfc
    mov eax,%2
    out dx,eax
%endmacro
%macro gpio_probe 0
    inc ebp
    mov dx,0x80ce
    in al,dx
    check al,0
    in ax,dx
    check ax,0
    in eax,dx
    check eax,0
    mov dx,0x80d9
    in al,dx
    check al,0
    inc dx
    in al,dx
    check al,0xff
    mov dx,0x80c0
    in al,dx
    mov bl,al
    in al,dx
    xor al,bl
    check al,0x20
%endmacro
start:
    rep_test_init
    gpio_probe
    config 0x10,0x8001
    config 0x04,1
    gpio_probe
    config 0x10,0x9001
    gpio_probe
    mov dx,0x90ce
    in al,dx
    check al,0xff ; no relocated GPIO alias
    config 0x04,0
    gpio_probe
    mov dx,0x80d6
    mov al,5
    out dx,al
    xor al,al
    out dx,al
    mov dword [R],0x4b4f5047 ; GPOK
.halt:
    cli
    hlt
    jmp .halt
rep_test_handlers
times 0x100000-($-$$) db 0xff
