; SPDX-License-Identifier: GPL-2.0-or-later
; Every SHUFPS selector, same/different/memory source, four XMM offsets.
; Includes signed zero, NaN payloads, infinities and subnormal bit patterns.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro shuffle_case 4
    inc ebp
    movups %1,[pattern_a]
    %if %3 == 1
        movups %2,[pattern_b]
    %endif
    pushfd
    pop edi
    %if %3 == 0
        shufps %1,%1,%4
    %elif %3 == 1
        shufps %1,%2,%4
    %else
        shufps %1,[pattern_b],%4
    %endif
    pushfd
    pop esi
    check esi,edi
    movups [0x4000],%1
    mov eax,[pattern_a+4*((%4>>0)&3)]
    check dword [0x4000],eax
    mov eax,[pattern_a+4*((%4>>2)&3)]
    check dword [0x4004],eax
    %if %3 == 0
        mov eax,[pattern_a+4*((%4>>4)&3)]
    %else
        mov eax,[pattern_b+4*((%4>>4)&3)]
    %endif
    check dword [0x4008],eax
    %if %3 == 0
        mov eax,[pattern_a+4*((%4>>6)&3)]
    %else
        mov eax,[pattern_b+4*((%4>>6)&3)]
    %endif
    check dword [0x400c],eax
%endmacro
%macro shuffle_register 2
    %assign selector 0
    %rep 256
        shuffle_case %1,%2,0,selector
        shuffle_case %1,%2,1,selector
        shuffle_case %1,%2,2,selector
        %assign selector selector+1
    %endrep
%endmacro
start:
    rep_test_init
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    mov eax,cr4
    or eax,0x200
    mov cr4,eax
    shuffle_register xmm0,xmm1
    shuffle_register xmm1,xmm0
    shuffle_register xmm3,xmm0
    shuffle_register xmm7,xmm0
    check ebp,3073
    mov dword [R],0x4b4f4853 ; SHOK
.halt:
    cli
    hlt
    jmp .halt
align 16
pattern_a: dd 0,0x80000000,0x7fc12345,0x7f812345
pattern_b: dd 1,0x7f800000,0xff800000,0x3f800001
    rep_test_handlers
times 0x100000-($-$$) db 0xff
