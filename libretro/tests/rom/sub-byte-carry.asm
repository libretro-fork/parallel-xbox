; SPDX-License-Identifier: GPL-2.0-or-later
; Exhaustive byte borrow at a block entry, with dirty upper operand bits.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro subtract 0
    mov eax,esi
    or eax,0x8000ff00
    mov ecx,edi
    or ecx,0x7fff0000
    sub al,cl
%endmacro
start:
    rep_test_init
    xor esi,esi
.lhs:
    xor edi,edi
.rhs:
    inc ebp
    cmp esi,edi
    setb byte [R+64] ; independent 32-bit comparison of unsigned bytes
    subtract
    pushfd
    pop dword [R+68]
    and dword [R+68],0x8d5

    subtract
    jmp .setc
.setc:
    setc dl
    pushfd
    pop eax
    and eax,0x8d5
    check eax,[R+68]
    check dl,[R+64]

    subtract
    jmp .adc
.adc:
    mov edx,0
    adc edx,0
    movzx ecx,byte [R+64]
    check edx,ecx

    subtract
    jmp .inc
.inc:
    inc edx
    setc dl
    check dl,[R+64]

    subtract
    jmp .branch
.branch:
    mov edx,0
    jc .borrow
    jmp .joined
.borrow:
    mov edx,1
.joined:
    movzx ecx,byte [R+64]
    check edx,ecx

    ; Shared indirect entry and a flags-transparent outgoing block.
    subtract
    mov ebx,.returned
    jmp pass
.returned:
    check dl,[R+64]
    inc edi
    cmp edi,256
    jb .rhs
    inc esi
    cmp esi,256
    jb .lhs
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
%macro fp_carry 1
    inc ebp
    fninit
    mov word [0x6300],%1
    fldcw [0x6300]
    xor eax,eax
    sub al,1
    jmp %%fp_entry
%%fp_entry:
    fld1
    fadd dword [one]
    mov edx,0
    adc edx,0 ; entry carry is read with native FP arithmetic still pending
    fstp qword [0x6310]
    check edx,1
    check dword [0x6310],0
    check dword [0x6314],0x40000000
%endmacro
    fp_carry 0x7f
    fp_carry 0x37f
    check ebp,65539
    mov dword [R],0x4b4f4253 ; SBOK
.halt:
    cli
    hlt
    jmp .halt
pass:
    lea edx,[edx+1]
    jmp carry
carry:
    setc dl
    jmp ebx
one: dd 1.0
    rep_test_handlers
times 0x100000-($-$$) db 0xff
