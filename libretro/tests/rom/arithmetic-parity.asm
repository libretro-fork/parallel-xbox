; SPDX-License-Identifier: GPL-2.0-or-later
; Independent low-result parity, plus full flags retained after predicates.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro produce 5
    mov eax,esi
    or eax,%5
    push dword %4
    popfd
%ifidni %1,inc
    inc %2
%elifidni %1,dec
    dec %2
%elifidni %1,neg
    neg %2
%else
    %1 %2,%3
%endif
%endmacro
%macro parity_case 5
    inc ebp
    ; Snapshot the normal full-flags path before exercising any PF-only
    ; consumer. PF itself is checked against a fixed parity table.
    produce %1,%2,%3,%4,%5
    mov [0x6204],eax
    pushfd
    pop dword [0x6200]
    and dword [0x6200],0x8d5
    movzx edx,byte [0x6204]
    movzx edi,byte [parity+edx]
%ifidni %1,shl
%define is_shift 1
%elifidni %1,shr
%define is_shift 1
%elifidni %1,sar
%define is_shift 1
%else
%define is_shift 0
%endif
%if is_shift && %3 == 0
    mov edi,((%4 >> 2) & 1) ; a zero shift preserves the incoming PF
%endif
    mov eax,[0x6200]
    shr eax,2
    and eax,1
    check eax,edi

    produce %1,%2,%3,%4,%5
    mov edx,0xa5a50000
    setp dl
    setnp dh
    pushfd
    pop ecx
    and ecx,0x8d5
    check ecx,[0x6200]
    mov eax,edi
    check dl,al
    xor al,1
    check dh,al
    and edx,0xffff0000
    check edx,0xa5a50000

    produce %1,%2,%3,%4,%5
    mov ecx,0
    mov ebx,0
    mov edx,1
    cmovp ecx,edx
    cmovnp ebx,edx
    pushfd
    pop eax
    and eax,0x8d5
    check eax,[0x6200]
    check ecx,edi
    mov eax,edi
    xor eax,1
    check ebx,eax

    produce %1,%2,%3,%4,%5
    mov ecx,0
    jp %%even
    jmp %%joined
%%even:
    mov ecx,1
%%joined:
    pushfd
    pop eax
    and eax,0x8d5
    check eax,[0x6200]
    check ecx,edi

    produce %1,%2,%3,%4,%5
    mov ecx,0
    jnp %%odd
    jmp %%joined2
%%odd:
    mov ecx,1
%%joined2:
    pushfd
    pop eax
    and eax,0x8d5
    check eax,[0x6200]
    mov eax,edi
    xor eax,1
    check ecx,eax
%endmacro
%macro cases 3
    parity_case add,%1,0x81,%2,%3
    parity_case adc,%1,0x81,%2,%3
    parity_case sub,%1,0x81,%2,%3
    parity_case sbb,%1,0x81,%2,%3
    parity_case inc,%1,0,%2,%3
    parity_case dec,%1,0,%2,%3
    parity_case neg,%1,0,%2,%3
%assign count 0
%rep 3
    parity_case shl,%1,count,%2,%3
    parity_case shr,%1,count,%2,%3
    parity_case sar,%1,count,%2,%3
%if count == 0
%assign count 1
%else
%assign count 7
%endif
%endrep
%endmacro
start:
    rep_test_init
    xor esi,esi
.loop:
%assign pattern 0
%rep 3
    cases al,0x2,pattern
    cases ax,0x2,pattern
    cases eax,0x2,pattern
    cases al,0x8d7,pattern
    cases ax,0x8d7,pattern
    cases eax,0x8d7,pattern
%if pattern == 0
%assign pattern 0x7fffff00
%else
%assign pattern 0x80008000
%endif
%endrep
    inc esi
    cmp esi,256
    jb .loop
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
%macro live_fp 1
    inc ebp
    fninit
    mov word [0x6300],%1
    fldcw [0x6300]
    fld1
    fadd dword [one]
    mov eax,0x7f
    add al,1
    setp dl ; flush pending FP state through the new arithmetic PF path
    fstp qword [0x6310]
    check dl,0
    check dword [0x6310],0
    check dword [0x6314],0x40000000
    fnstenv [0x6400]
    check word [0x6408],0xffff
%endmacro
    live_fp 0x7f
    live_fp 0x37f
    check ebp,73731
    mov dword [R],0x4b4f5041 ; APOK
.halt:
    cli
    hlt
    jmp .halt
one: dd 1.0
parity:
%assign n 0
%rep 256
    db 1 ^ (((n>>0)+(n>>1)+(n>>2)+(n>>3)+(n>>4)+(n>>5)+(n>>6)+(n>>7)) & 1)
%assign n n+1
%endrep
    rep_test_handlers
times 0x100000-($-$$) db 0xff
