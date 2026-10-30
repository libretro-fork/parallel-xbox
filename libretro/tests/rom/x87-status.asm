; SPDX-License-Identifier: GPL-2.0-or-later
; Getter widths, status/TOP packing, every control/status word and live stack.
bits 32
org 0xfff00000
%include "rep-common.inc"
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
    mov dword [R],0x4b4f5753 ; SWOK
.halt:
    cli
    hlt
    jmp .halt
    rep_test_handlers
times 0x100000-($-$$) db 0xff
