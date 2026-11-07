; SPDX-License-Identifier: GPL-2.0-or-later
; PRAMIN alias oracle. UNIFIED=0 tests legacy snapshot/128 MiB behavior.
%ifndef UNIFIED
%define UNIFIED 1
%endif
%ifndef WAIT
%define WAIT 50000000
%endif
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro cfg 2
    mov dx,0xcf8
    mov eax,%1
    out dx,eax
    mov dx,0xcfc
    mov eax,%2
    out dx,eax
%endmacro
start:
    rep_test_init
    cfg 0x8000f018,0x00010100
    cfg 0x8000f020,0xfdf0fd00
    cfg 0x8000f024,0xf7f0f000
    cfg 0x8000f004,7
    cfg 0x80010010,0xfd000000
    cfg 0x80010014,0xf0000000
    cfg 0x80010004,6
    mov ebp,2
    mov dword [0x03feffe0],0x12345678
    mov dword [0xfd710010],0x11223344
    cmp dword [0xfd710010],0x11223344
    jne fail
%if UNIFIED
    cmp dword [0x03feffe0],0x11223344
%else
    cmp dword [0x03feffe0],0x12345678
%endif
    jne fail
    mov ebp,3
%if UNIFIED
    mov dword [0x03feffe4],0x55667788
    cmp dword [0xfd710014],0x55667788
    jne fail
%endif
    mov dword [0xfd70000f],0xaabbccdd
    cmp dword [0xfd70000f],0xaabbccdd
    jne fail
%if UNIFIED
    cmp byte [0x03ffffff],0xdd
    jne fail
    cmp word [0x03ffffe0],0xbbcc
    jne fail
    cmp byte [0x03ffffe2],0xaa
    jne fail
%endif
%if UNIFIED
    mov ebp,5
    mov dword [0x03fefff0],0x000007b8
    mov word [0x03fefff4],0xc300
    mov edx,0x03fefff0
    call edx
    cmp eax,7
    jne fail
    mov dword [0xfd710001],9
    call edx
    cmp eax,9
    jne fail
%endif
    mov dword [R],0x504d5741 ; AWMP: initial checks complete, delay for state
    mov ecx,WAIT
.delay:
    dec ecx
    jnz .delay
    mov ebp,4
    cmp dword [0xfd710010],0x11223344
    jne fail
%if UNIFIED
    cmp dword [0x03feffe0],0x11223344
%else
    cmp dword [0x03feffe0],0x12345678
%endif
    jne fail
    cmp dword [0xfd70000f],0xaabbccdd
    jne fail
    mov dword [R],0x4b4f4d50 ; PMOK
.halt:
    cli
    hlt
    jmp .halt
rep_test_handlers
times 0x100000-($-$$) db 0xff
