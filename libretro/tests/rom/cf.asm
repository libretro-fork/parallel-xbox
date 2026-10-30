; SPDX-License-Identifier: GPL-2.0-or-later
; CF across translation block boundaries. Every flag-setting instruction,
; with operands around the carry and CF in both ways, sets CF; a jump ends
; the block, and the next block reads CF before setting the flags: INC
; (which keeps CF), ADC, JC, and a block shared by every case (reached from
; blocks of every CC_OP, left by an indirect jump). Each must see the CF
; SETC saw right after the instruction. Two more shared blocks, which CPU
; Boost translates once for every class of CC_OP (TB_CF_ANY) or must not:
; one that sets the flags before reading CF, and one that leaves by a
; direct jump, flags untouched, for a block that reads CF. Run with CPU
; Boost on and off (run.sh). Results at 0x300000: R+0 "CFOK" when every
; case passed, R+4 the number of the case that failed.
bits 32
org 0xfff00000
%include "rep-common.inc"

; Optional second build: establish the interrupt shadow instead of a JMP.
; The very next instruction must still observe the original carry class.
%macro cf_boundary 1
%ifdef STI_BOUNDARY
    sti
%else
    jmp %1
%endif
%endmacro

%macro popf_cf1 0
    push dword 0x003
    popfd
%endmacro
%macro popf_cf0 0
    push dword 0x002
    popfd
%endmacro

; the instruction (%1) on EAX = %2, ECX = %3 with CF = %4 before it
%macro cfset 4
    mov eax,%2
    mov ecx,%3
%if %4
    stc
%else
    clc
%endif
    %1
%endmacro

%macro cfone 4
    inc ebp
    cfset {%1},%2,%3,%4
    setc byte [R+64]
    cf_boundary %%a
%%a:
    inc ebx
    setc dl
    check dl,[R+64]
    cfset {%1},%2,%3,%4
    cf_boundary %%b
%%b:
    mov edx,0
    adc edx,0
    movzx esi,byte [R+64]
    check edx,esi
    cfset {%1},%2,%3,%4
    cf_boundary %%c
%%c:
    jc %%set
    check byte [R+64],0
    jmp %%d
%%set:
    check byte [R+64],1
%%d:
    cfset {%1},%2,%3,%4
    mov esi,%%e
    jmp shared
%%e:
    check dl,[R+64]
    cfset {%1},%2,%3,%4
    mov esi,%%f
    jmp pass
%%f:
    check dl,[R+64]
    cfset {%1},%2,%3,%4
    mov dh,0
    mov esi,%%g
    jmp ownflags
%%g:
    check dh,1
%endmacro

; every operand pair, with CF clear and set before
%macro cfprod 1
    cfone {%1},0x0,0x0,0
    cfone {%1},0x0,0x0,1
    cfone {%1},0x1,0x1,0
    cfone {%1},0x1,0x1,1
    cfone {%1},0xffffffff,0x1,0
    cfone {%1},0xffffffff,0x1,1
    cfone {%1},0x80000000,0x80000000,0
    cfone {%1},0x80000000,0x80000000,1
    cfone {%1},0x5,0x7,0
    cfone {%1},0x5,0x7,1
    cfone {%1},0x7fffffff,0x1,0
    cfone {%1},0x7fffffff,0x1,1
    cfone {%1},0x12345678,0x9abcdef0,0
    cfone {%1},0x12345678,0x9abcdef0,1
%endmacro

start:
    rep_test_init
%ifdef STI_BOUNDARY
    ; This variant tests carry, not delivery: mask both PICs before STI.
    mov al,0xff
    out 0x21,al
    out 0xa1,al
%endif
    xor ebp,ebp
    mov edi,2
.pass:
    cfprod {add eax,ecx}
    cfprod {sub eax,ecx}
    cfprod {cmp eax,ecx}
    cfprod {and eax,ecx}
    cfprod {or eax,ecx}
    cfprod {xor eax,ecx}
    cfprod {test eax,ecx}
    cfprod {inc eax}
    cfprod {dec eax}
    cfprod {neg eax}
    cfprod {adc eax,ecx}
    cfprod {sbb eax,ecx}
    cfprod {sar eax,1}
    cfprod {shl eax,1}
    cfprod {shr eax,1}
    cfprod {mul ecx}
    cfprod {imul eax,ecx}
    cfprod {add al,cl}
    cfprod {sub ax,cx}
    cfprod {bt eax,ecx}
    cfprod {popf_cf1}
    cfprod {popf_cf0}
    cfprod {stc}
    cfprod {clc}
    cfprod {rcl eax,1}
    dec edi
    jnz .pass
    mov dword [R],0x4b4f4643
.halt:
    cli
    hlt
    jmp .halt
shared:
%ifdef STI_BOUNDARY
    sti
%endif
    inc ebx
    setc dl
    jmp esi
; flags untouched: its exit's CC_OP is the one it started with
pass:
    lea ebx,[ebx+1]
    cf_boundary pass_cf
pass_cf:
    setc dl
    jmp esi
; sets the flags first: the same code whatever it starts with
ownflags:
    mov ecx,1
    sub ecx,2
    cf_boundary ownflags_cf
ownflags_cf:
    setc dh
    jmp esi
    rep_test_handlers
times 0x100000-($-$$) db 0xff
