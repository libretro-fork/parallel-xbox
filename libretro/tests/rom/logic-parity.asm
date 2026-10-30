; SPDX-License-Identifier: GPL-2.0-or-later
; Low-byte parity and retained lazy flags across SETcc, CMOVcc and branches.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro produce 2
    mov eax,esi
    or eax,0x80008000
%ifidni %1,test
    test %2,%2
%elifidni %1,and
    and %2,%2
%elifidni %1,or
    or %2,%2
%else
    xor %2,0
%endif
%endmacro
%macro parity_case 3
    inc ebp
    movzx edi,byte [parity+esi]
    shl edi,2
%if %3 == 8
    test esi,esi
    jnz %%nonzero
    or edi,0x40
%%nonzero:
    mov eax,esi
    and eax,0x80
    or edi,eax
%else
    or edi,0x80 ; dirty high bits make the wider result negative and nonzero
%endif
    produce %1,%2
    mov edx,0xa5a50000
    setp dl
    setnp dh
    pushfd
    pop eax
    and eax,0x8d5
    check eax,edi
    movzx eax,byte [parity+esi]
    check dl,al
    xor al,1
    check dh,al
    and edx,0xffff0000
    check edx,0xa5a50000

    produce %1,%2
    mov ecx,0
    mov edx,1
    cmovp ecx,edx
    pushfd
    pop eax
    and eax,0x8d5
    check eax,edi
    movzx eax,byte [parity+esi]
    check ecx,eax

    produce %1,%2
    mov ecx,0
    jp %%even
    jmp %%joined
%%even:
    mov ecx,1
%%joined:
    pushfd
    pop eax
    and eax,0x8d5
    check eax,edi
    movzx eax,byte [parity+esi]
    check ecx,eax

    produce %1,%2
    mov ecx,0
    jnp %%odd
    jmp %%joined2
%%odd:
    mov ecx,1
%%joined2:
    pushfd
    pop eax
    and eax,0x8d5
    check eax,edi
    movzx eax,byte [parity+esi]
    xor eax,1
    check ecx,eax
%endmacro
start:
    rep_test_init
    xor esi,esi
.loop:
%macro cases 1
    parity_case %1,al,8
    parity_case %1,ax,16
    parity_case %1,eax,32
%endmacro
    cases test
    cases and
    cases or
    cases xor
    inc esi
    cmp esi,256
    jb .loop
    check ebp,3073
    mov dword [R],0x4b4f4650 ; PFOK
.halt:
    cli
    hlt
    jmp .halt
parity:
%assign n 0
%rep 256
    db 1 ^ (((n>>0)+(n>>1)+(n>>2)+(n>>3)+(n>>4)+(n>>5)+(n>>6)+(n>>7)) & 1)
%assign n n+1
%endrep
    rep_test_handlers
times 0x100000-($-$$) db 0xff
