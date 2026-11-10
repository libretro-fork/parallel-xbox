; SPDX-License-Identifier: GPL-2.0-or-later
; Getter widths, status/TOP packing, every control/status word and live stack.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro integer_status_case 4
    inc ebp
    fninit
    mov word [0x6030],%1
    fldcw [0x6030]
    mov dword [0x6034],%2
    fld dword [0x6034]
    fist word [0x6038]
    check word [0x6038],%3
    fnstsw ax
    and eax,0x21 ; invalid and precision/inexact
    mov [R+0x20],eax
    check eax,%4
%endmacro
%macro round_status_case 3
    inc ebp
    fninit
    mov word [0x6030],%1
    fldcw [0x6030]
    mov dword [0x6034],%2
    fld dword [0x6034]
    frndint
    fnstsw ax
    and eax,0x21
    check eax,0x20
    fstp dword [0x6038]
    check dword [0x6038],%3
%endmacro
%macro bcd_status_case 6
    inc ebp
    fninit
    mov word [0x6030],%1
    fldcw [0x6030]
    mov dword [0x6034],%2
    fld dword [0x6034]
    mov dword [0x6040],0xdeadbeef
    mov dword [0x6044],0xdeadbeef
    mov word [0x6048],0xface
    fbstp [0x6040]
    fnstsw ax
    and eax,0x21
    check eax,%6
    check dword [0x6040],%3
    check dword [0x6044],%4
    check word [0x6048],%5
%endmacro
%macro scale_status_case 3
    inc ebp
    fninit
    mov dword [0x6034],%1
    fld dword [0x6034]
    fld1
    fscale
    fnstsw ax
    and eax,0x39 ; invalid, overflow, underflow, precision
    check eax,%3
    fstp dword [0x6038]
    check dword [0x6038],%2
%endmacro
start:
    rep_test_init
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    fninit
    mov dword [0x5008],0xffff ; all stack entries empty
    mov word [0x5000],0x37f
    xor esi,esi
.status:
    mov [0x5004],si
    fldenv [0x5000]
    mov eax,0xa5a50000
    fnstsw ax
    mov edx,esi
    and edx,~0x8000
    test edx,0x80 ; busy mirrors exception summary when loading an environment
    jz .no_busy
    or edx,0x8000
.no_busy:
    or edx,0xa5a50000
    check eax,edx
    mov dword [0x6000],0xcafe0000
    fnstsw [0x6000]
    xor edx,0xa5a50000 ^ 0xcafe0000
    check dword [0x6000],edx
    inc esi
    cmp esi,0x10000
    jb .status
    inc ebp
    fninit
    xor esi,esi
.control:
    mov [0x6000],si
    fldcw [0x6000]
    mov dword [0x6004],0xcafe0000
    fnstcw [0x6004]
    mov edx,esi
    or edx,0xcafe0000
    check dword [0x6004],edx
    inc esi
    cmp esi,0x10000
    jb .control
    inc ebp
    fninit
    ; Exercise TOP transitions while stack values are held in generated FP regs.
    fld1
    fnstsw ax
    check ax,0x3800
    fnstcw [0x6000]
    check word [0x6000],0x37f
    fld1
    fadd st0,st1
    fnstsw [0x6004]
    check word [0x6004],0x3000
    fstp dword [0x6010]
    check dword [0x6010],0x40000000
    fnstsw ax
    check ax,0x3800
    fstp dword [0x6010]
    check dword [0x6010],0x3f800000
    fnstsw ax
    check ax,0
    ; Ordered FCOMI must set invalid for a quiet NaN; FUCOMI must not.
    ; Both report unordered in EFLAGS. Exercise real hard/soft helpers.
    mov dword [0x6020],0x123
    mov dword [0x6024],0xc0000000
    mov word [0x6028],0x7fff
    fninit
    fld tword [0x6020]
    fld1
    fcomi st0,st1
    pushfd
    pop eax
    and eax,0x45
    inc ebp
    mov [R+0x10],eax
    check eax,0x45
    fnstsw ax
    and eax,1
    inc ebp
    mov [R+0x14],eax
    check eax,1
    fninit
    fld tword [0x6020]
    fld1
    fucomi st0,st1
    pushfd
    pop eax
    and eax,0x45
    inc ebp
    mov [R+0x18],eax
    check eax,0x45
    fnstsw ax
    and eax,1
    inc ebp
    mov [R+0x1c],eax
    check eax,0
    ; FIST m16 takes the real helper in both hard and soft FPU modes.
    integer_status_case 0x037f,0x3fc00000,2,0x20
    integer_status_case 0x077f,0x3fc00000,1,0x20
    integer_status_case 0x0b7f,0x3fc00000,2,0x20
    integer_status_case 0x0f7f,0x3fc00000,1,0x20
    integer_status_case 0x037f,0xbfc00000,-2,0x20
    integer_status_case 0x077f,0xbfc00000,-2,0x20
    integer_status_case 0x0b7f,0xbfc00000,-1,0x20
    integer_status_case 0x0f7f,0xbfc00000,-1,0x20
    integer_status_case 0x037f,0x47000000,0x8000,1
    integer_status_case 0x037f,0x7fc00123,0x8000,1
    round_status_case 0x037f,0x3fc00000,0x40000000
    round_status_case 0x077f,0x3fc00000,0x3f800000
    round_status_case 0x0b7f,0x3fc00000,0x40000000
    round_status_case 0x0f7f,0x3fc00000,0x3f800000
    round_status_case 0x037f,0xbfc00000,0xc0000000
    round_status_case 0x077f,0xbfc00000,0xc0000000
    round_status_case 0x0b7f,0xbfc00000,0xbf800000
    round_status_case 0x0f7f,0xbfc00000,0xbf800000
    bcd_status_case 0x037f,0x3fc00000,2,0,0,0x20
    bcd_status_case 0x077f,0x3fc00000,1,0,0,0x20
    bcd_status_case 0x0b7f,0xbfc00000,1,0,0x8000,0x20
    bcd_status_case 0x0f7f,0xbfc00000,1,0,0x8000,0x20
    bcd_status_case 0x037f,0x7fc00123,0,0xc0000000,0xffff,1
    bcd_status_case 0x037f,0x7f800000,0,0xc0000000,0xffff,1
    scale_status_case 0x469c4000,0x7f800000,0x28 ; 2^20000 overflows
    scale_status_case 0xc69c4000,0,0x30 ; 2^-20000 underflows
    mov dword [R],0x4b4f5753 ; SWOK
.halt:
    cli
    hlt
    jmp .halt
    rep_test_handlers
times 0x100000-($-$$) db 0xff
