; SPDX-License-Identifier: GPL-2.0-or-later
; Dynamic-to-dynamic flag updates must invalidate the incoming class proof.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro carry_case 2
    inc ebp
    xor eax,eax
    sub al,1
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shl eax,cl
    mov edx,0
    adc edx,0
    check edx,%2
%endmacro
%macro relation_case 1
    inc ebp
    mov eax,0x80000000
    sub eax,1
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shr eax,cl
    setl dl
    check dl,1
%endmacro
%macro sahf_case 2
    inc ebp
    xor eax,eax
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shr eax,cl
    mov ah,0
    sahf
    seto dl
    check dl,%2
%endmacro
start:
    rep_test_init
    carry_case 0,1
    carry_case 1,1
    carry_case 2,0
    carry_case 31,0
    relation_case 0
    relation_case 1
    sahf_case 0,0
    sahf_case 1,1
    check ebp,9
    mov dword [R],0x4b4f4945 ; EIOK
.halt:
    cli
    hlt
    jmp .halt
    rep_test_handlers
times 0x100000-($-$$) db 0xff
