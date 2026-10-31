; SPDX-License-Identifier: GPL-2.0-or-later
; EMMS empties tags without modifying TOP, payloads, control or EFLAGS.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro snapshot_case 2
    inc ebp
    fninit
    mov word [0x6000],%1
    fldcw [0x6000]
    fld1
    fld qword [two]
    fadd st0,st1
%rep %2
    fincstp
%endrep
    fxsave [0x4000]
    push dword 0x8d7
    popfd
    emms
    pushfd
    pop eax
    and eax,0x8d5
    check eax,0x8d5
    fxsave [0x5000]
    check byte [0x5004],0
    ; Compare every saved byte except the tag byte, including TOP, FIP,
    ; x87/MMX payloads and XMM registers. Buffers started identically.
    mov byte [0x4004],0
    mov esi,0x4000
    mov edi,0x5000
    mov ecx,128
    repe cmpsd
    jne fail
    ; Repeated EMMS remains empty and permits a new x87 push.
    emms
    fld1
    fstp qword [0x6100]
    check dword [0x6100],0
    check dword [0x6104],0x3ff00000
%endmacro
start:
    rep_test_init
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    mov eax,cr4
    or eax,0x200
    mov cr4,eax
    mov edi,0x4000
    xor eax,eax
    mov ecx,2048
    rep stosd
%assign top 0
%rep 8
    snapshot_case 0x7f,top
    snapshot_case 0x37f,top
%assign top top+1
%endrep
    ; Live arithmetic immediately before EMMS must be published even
    ; without FXSAVE first forcing the pending x87 registers to memory.
    inc ebp
    fninit
    fld1
    fld qword [two]
    fadd st0,st1
    emms
    fxsave [0x5000]
    check byte [0x5004],0
    check word [0x5028],0x4000
    check dword [0x5024],0xc0000000
    ; Enter MMX after an empty stack, checking all payloads and full tags.
    inc ebp
    movq mm0,[payload]
    movq mm1,[payload]
    movq mm2,[payload]
    movq mm3,[payload]
    movq mm4,[payload]
    movq mm5,[payload]
    movq mm6,[payload]
    movq mm7,[payload]
    fxsave [0x4000]
    check byte [0x4004],0xff
    check word [0x4002],0
    emms
    fxsave [0x5000]
    check byte [0x5004],0
    mov byte [0x4004],0
    mov esi,0x4000
    mov edi,0x5000
    mov ecx,128
    repe cmpsd
    jne fail
    check ebp,19
    mov dword [R],0x4b4f4d4d ; MMOK
.halt:
    cli
    hlt
    jmp .halt
align 8
two: dq 2.0
payload: dq 0x0123456789abcdef
    rep_test_handlers
times 0x100000-($-$$) db 0xff
