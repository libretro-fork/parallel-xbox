; SPDX-License-Identifier: GPL-2.0-or-later
; FT0 is restaged within every instruction, including helper comparisons.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro reset_case 1
    inc ebp
    fninit
    mov word [0x6000],%1
    fldcw [0x6000]
%endmacro
%macro empty_stack 1
    fnstenv [0x5000]
    check word [0x5000],%1
    check word [0x5008],0xffff
    movzx eax,word [0x5004]
    and eax,0x3800
    check eax,0
%endmacro
%macro scratch_cases 1
    reset_case %1
    fld dword [one]
    fadd dword [two]
    jmp %%after_add
%%after_add:
    xorps xmm0,xmm0
    movmskps eax,xmm0 ; unrelated helper flushes the real stack registers
    check eax,0
    fmul dword [four]
    fsub dword [three]
    fdiv dword [three]
    fstp dword [0x4000]
    check dword [0x4000],0x40400000
    empty_stack %1

    reset_case %1
    fld qword [two64]
    fadd qword [three64]
    fmul qword [two64]
    fsub qword [two64]
    fdiv qword [four64]
    fstp qword [0x4000]
    check dword [0x4000],0
    check dword [0x4004],0x40000000
    empty_stack %1

    reset_case %1
    fld1
    fiadd dword [int3]
    fimul word [int2]
    fisub word [int3]
    fidiv dword [int5]
    fstp dword [0x4000]
    check dword [0x4000],0x3f800000
    empty_stack %1

    reset_case %1
    fld1
    fld qword [two64]
    fadd st0,st1
    fmul st0,st1
    fsub st0,st1
    fdiv st0,st1
    fucomi st0,st1
    pushfd
    pop eax
    and eax,0x45
    check eax,0
    fxch st1
    fucomi st0,st1
    pushfd
    pop eax
    and eax,0x45
    check eax,1
    fcomi st0,st0
    pushfd
    pop eax
    and eax,0x45
    check eax,0x40
    fucomip st0,st1
    pushfd
    pop eax
    and eax,0x45
    check eax,1
    ftst
    fnstsw ax
    and eax,0x4500
    check eax,0
    fstp dword [0x4000]
    check dword [0x4000],0x40000000
    empty_stack %1

    reset_case %1
    fld1
    fld1
    fucompp
    fnstsw ax
    and eax,0x7d00
    check eax,0x4000
    empty_stack %1

    reset_case %1
    fldz
    ftst
    fnstsw ax
    and eax,0x4500
    check eax,0x4000
    fstp dword [0x4000]
    check dword [0x4000],0
    empty_stack %1

    reset_case %1
    fld1
    fadd dword [half_ulp]
    xorps xmm0,xmm0
    movmskps eax,xmm0
    fstp dword [0x4000]
    %if (%1 & 0xc00) == 0x800
        check dword [0x4000],0x3f800001
    %else
        check dword [0x4000],0x3f800000
    %endif
    empty_stack %1

    reset_case %1
    fld dword [negative_one]
    fadd dword [negative_half_ulp]
    fstp dword [0x4000]
    %if (%1 & 0xc00) == 0x400
        check dword [0x4000],0xbf800001
    %else
        check dword [0x4000],0xbf800000
    %endif
    empty_stack %1
%endmacro
start:
    rep_test_init
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    mov eax,cr4
    or eax,0x200
    mov cr4,eax
%assign precision 0
%rep 3
    %assign pc 0
    %if precision == 1
        %assign pc 0x200
    %elif precision == 2
        %assign pc 0x300
    %endif
    %assign rounding 0
    %rep 4
        scratch_cases (0x7f | pc | (rounding << 10))
        %assign rounding rounding+1
    %endrep
    %assign precision precision+1
%endrep
    check ebp,97
    mov dword [R],0x4b4f5058 ; XPOK
.halt:
    cli
    hlt
    jmp .halt
align 8
one: dd 1.0
two: dd 2.0
three: dd 3.0
four: dd 4.0
two64: dq 2.0
three64: dq 3.0
four64: dq 4.0
negative_one: dd -1.0
half_ulp: dd 0.000000059604644775390625
negative_half_ulp: dd -0.000000059604644775390625
int2: dd 2
int3: dd 3
int5: dd 5
    rep_test_handlers
times 0x100000-($-$$) db 0xff
