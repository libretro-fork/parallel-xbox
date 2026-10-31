; SPDX-License-Identifier: GPL-2.0-or-later
; Precision control rounds arithmetic, never the values loaded or moved.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro precision_case 2
    inc ebp
    fninit
    mov word [0x6000],%1
    fldcw [0x6000]
    fld qword [load_input]
    fld st0
    fxch st1
    fstp st1
    fst qword [0x6010]
    mov eax,[load_input]
    check dword [0x6010],eax
    mov eax,[load_input+4]
    check dword [0x6014],eax
    fnstsw ax ; publish the cache at a helper/status boundary
    fstp tword [0x6020]
    check dword [0x6020],0xdf309000
    check dword [0x6024],0x95cd850a
    check word [0x6028],0x401b
    fild dword [integer_input]
    fistp dword [0x6010]
    check dword [0x6010],16777217
    fld qword [one]
    fld qword [one_plus_tiny]
    fcom st1
    fnstsw ax
    and eax,0x4500
    check eax,0
    fstp st0
    fstp st0
    fld qword [arithmetic_input]
    fadd qword [one]
    fistp dword [0x6010]
    check dword [0x6010],%2
    fld qword [arithmetic_input]
    fld1
    faddp st1,st0
    fistp dword [0x6010]
    check dword [0x6010],%2
%endmacro
start:
    rep_test_init
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    precision_case 0x007f,16777216
    precision_case 0x047f,16777216
    precision_case 0x087f,16777218
    precision_case 0x0c7f,16777216
    precision_case 0x027f,16777217
    precision_case 0x067f,16777217
    precision_case 0x0a7f,16777217
    precision_case 0x0e7f,16777217
    precision_case 0x037f,16777217
    precision_case 0x077f,16777217
    precision_case 0x0b7f,16777217
    precision_case 0x0f7f,16777217
    mov dword [R],0x4b4f4c50 ; PLOK
.halt:
    cli
    hlt
    jmp .halt
rep_test_handlers
align 8
load_input: dq 314159265.35897934
integer_input: dd 16777217
align 8
one: dq 1.0
one_plus_tiny: dq 1.000000000931322574615478515625
arithmetic_input: dq 16777216.0
times 0x100000-($-$$) db 0xff
