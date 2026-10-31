; SPDX-License-Identifier: GPL-2.0-or-later
; SAHF overwrites S/Z/A/P/C, preserves O/DF and leaves GPRs untouched.
bits 32
org 0xfff00000
%include "rep-common.inc"
%assign total 0
%macro sahf_case 5
%assign total total+1
    inc ebp
    mov eax,%3
    mov ecx,%4
    push dword 0xcd7 ; OF and DF set before the producing instruction
    popfd
%ifidni %1,test
    test %2,%2
%elifidni %1,and
    and %2,%2
%elifidni %1,or
    or %2,%2
%elifidni %1,xor
    xor %2,0
%elifidni %1,inc
    inc %2
%elifidni %1,dec
    dec %2
%elifidni %1,nop
    nop
%else
    %1 %2,1
%endif
    mov ecx,esi
    mov ah,cl
    mov [R+64],eax
    mov ebx,0x11223344
    sahf
    pushfd
    pop edx
    and edx,0xcd7
    mov edi,esi
    and edi,0xd5
    or edi,(0x402 | (%5 << 11))
    check edx,edi
    check eax,[R+64]
    check ebx,0x11223344
    check ecx,esi
%endmacro
%macro logical_cases 2
    sahf_case test,%1,%2,0,0
    sahf_case and,%1,%2,0,0
    sahf_case or,%1,%2,0,0
    sahf_case xor,%1,%2,0,0
%endmacro
%macro shared_case 2
%assign total total+1
    inc ebp
    push dword 0xcd7
    popfd
%if %1 == 0
    xor eax,eax
%elif %1 == 1
    mov eax,0x7fffffff
    add eax,1
%elif %1 == 2
    mov eax,0x80000000
    sub eax,1
%elif %1 == 3
    mov eax,0x80000000
    sar eax,1
%else
    mov eax,0
%endif
    mov ecx,esi
    mov ah,cl
    mov [R+64],eax
    mov ebx,%%returned
    jmp shared
%%returned:
    and edx,0xcd7
    mov edi,esi
    and edi,0xd5
    or edi,(0x402 | (%2 << 11))
    check edx,edi
    check eax,[R+64]
    check ecx,esi
%endmacro
start:
    rep_test_init
    xor esi,esi
.loop:
    logical_cases al,0
    logical_cases ax,0
    logical_cases eax,0
    logical_cases al,0x80008080
    logical_cases ax,0x80008080
    logical_cases eax,0x80008080
    logical_cases al,0x7fffffff
    logical_cases ax,0x7fffffff
    logical_cases eax,0x7fffffff
    ; Other classes must retain overflow, including explicit flag state.
    sahf_case add,al,0x7f,0,1
    sahf_case add,ax,0x7fff,0,1
    sahf_case add,eax,0x7fffffff,0,1
    sahf_case add,eax,1,0,0
    sahf_case sub,al,0x80,0,1
    sahf_case sub,ax,0x8000,0,1
    sahf_case sub,eax,0x80000000,0,1
    sahf_case sub,eax,3,0,0
    sahf_case inc,al,0x7f,0,1
    sahf_case inc,ax,0x7fff,0,1
    sahf_case inc,eax,0x7fffffff,0,1
    sahf_case dec,al,0x80,0,1
    sahf_case dec,ax,0x8000,0,1
    sahf_case dec,eax,0x80000000,0,1
    sahf_case shl,al,0x40,0,1
    sahf_case shr,al,0x80,0,1
    sahf_case sar,al,0x80,0,0
    sahf_case nop,eax,0,0,1
    ; The same entry must distinguish the ZERO class from overflow-one
    ; and other classes; it must not acquire an unsafe ANY alias.
    shared_case 0,0
    shared_case 1,1
    shared_case 2,1
    shared_case 3,0
    shared_case 4,1
    inc esi
    cmp esi,256
    jb .loop
    check ebp,(total*256+1)
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
%macro fp_case 1
    inc ebp
    fninit
    mov word [0x6300],%1
    fldcw [0x6300]
    fld1
    fadd dword [one]
    mov eax,0x84ff0000
    test eax,eax
    mov ah,0x95
    sahf
    pushfd
    pop edx
    fstp qword [0x6310]
    and edx,0x8d5
    check edx,0x95
    check eax,0x84ff9500
    check dword [0x6310],0
    check dword [0x6314],0x40000000
%endmacro
    fp_case 0x7f
    fp_case 0x37f
    check ebp,(total*256+3)
    mov dword [R],0x4b4f4853 ; SHOK
.halt:
    cli
    hlt
    jmp .halt
shared:
    sahf
    pushfd
    pop edx
    jmp ebx
one: dd 1.0
    rep_test_handlers
times 0x100000-($-$$) db 0xff
