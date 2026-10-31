; SPDX-License-Identifier: GPL-2.0-or-later
; Relations must survive block boundaries without changing the lazy flags.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro produce 1
    mov eax,[R+80]
    mov ecx,[R+84]
%if %1 == 8
    or eax,0x8000ff00
    or ecx,0x7fff0000
    sub al,cl
%elif %1 == 32
    sub eax,ecx
%else
    add eax,ecx ; different entry class sharing the same predicate block
%endif
%endmacro
%macro relation 4
    inc ebp
    produce %1
    %2 byte [R+64] ; same-block reference uses the original operands
    pushfd
    pop dword [R+68]
    and dword [R+68],0x8d5

    produce %1
    jmp %%set
%%set:
    %2 dl
    pushfd
    pop ebx
    and ebx,0x8d5
    check ebx,[R+68]
    check dl,[R+64]

    produce %1
    mov edx,0
    mov ebx,1
    jmp %%cmov
%%cmov:
    %3 edx,ebx
    pushfd
    pop ebx
    and ebx,0x8d5
    check ebx,[R+68]
    movzx ecx,byte [R+64]
    check edx,ecx

    produce %1
    jmp %%branch
%%branch:
    mov edx,0
    %4 %%taken
    jmp %%joined
%%taken:
    mov edx,1
%%joined:
    pushfd
    pop ebx
    and ebx,0x8d5
    check ebx,[R+68]
    movzx ecx,byte [R+64]
    check edx,ecx

    produce %1
    mov ebx,%%returned
    jmp shared_ %+ %2
%%returned:
    check dl,[R+64]
%endmacro
%macro relations 1
    relation %1,setbe,cmovbe,jbe
    relation %1,seta,cmova,ja
    relation %1,setl,cmovl,jl
    relation %1,setge,cmovge,jge
    relation %1,setle,cmovle,jle
    relation %1,setg,cmovg,jg
%endmacro
start:
    rep_test_init
    xor esi,esi
.byte_lhs:
    xor edi,edi
.byte_rhs:
    mov [R+80],esi
    mov [R+84],edi
    relations 8
    inc edi
    cmp edi,256
    jb .byte_rhs
    inc esi
    cmp esi,256
    jb .byte_lhs
    xor esi,esi
.word_lhs:
    xor edi,edi
.word_rhs:
    mov eax,[values+esi*4]
    mov [R+80],eax
    mov eax,[values+edi*4]
    mov [R+84],eax
    relations 32
    relations 0
    inc edi
    cmp edi,16
    jb .word_rhs
    inc esi
    cmp esi,16
    jb .word_lhs
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
%macro fp_entry 1
    inc ebp
    fninit
    mov word [0x6300],%1
    fldcw [0x6300]
    mov eax,0x80000000
    sub eax,1
    jmp %%entry
%%entry:
    fld1
    fadd dword [one]
    setl dl
    fstp qword [0x6310]
    check dl,1
    check dword [0x6310],0
    check dword [0x6314],0x40000000
%endmacro
    fp_entry 0x7f
    fp_entry 0x37f
    check ebp,(65536*6+16*16*12+3)
    mov dword [R],0x4b4f4345 ; ECOK
.halt:
    cli
    hlt
    jmp .halt
%macro shared 1
shared_ %+ %1:
    %1 dl
    jmp ebx
%endmacro
    shared setbe
    shared seta
    shared setl
    shared setge
    shared setle
    shared setg
align 4
one: dd 1.0
values: dd 0,1,0x7f,0x80,0xff,0x7fff,0x8000,0xffff
        dd 0x7ffffffe,0x7fffffff,0x80000000,0x80000001
        dd 0xfffffffe,0xffffffff,0x55555555,0xaaaaaaaa
    rep_test_handlers
times 0x100000-($-$$) db 0xff
