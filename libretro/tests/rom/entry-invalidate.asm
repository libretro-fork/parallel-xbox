; SPDX-License-Identifier: GPL-2.0-or-later
; Dynamic-to-dynamic flag updates must invalidate the incoming class proof.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro carry_case 2
    inc ebp
    xor eax,eax
    sub al,1
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shl eax,cl
    jmp %%read
%%read:
    mov edx,0
    adc edx,0
    check edx,%2
%endmacro
%macro relation_case 1
    inc ebp
    mov eax,0x80000000
    sub eax,1
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shr eax,cl
    setl dl
    check dl,1
%endmacro
%macro sahf_case 2
    inc ebp
    xor eax,eax
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shr eax,cl
    mov ah,0
    sahf
    seto dl
    check dl,%2
%endmacro
; These classes remain valid for both zero and nonzero counts. A jump
; after the shift exercises the direct-exit proof as well as flag consumers.
%macro preserved_case 3
    inc ebp
%if %1 == 3
    push dword 3
    popfd
%else
    xor eax,eax
    sub ax,1
%endif
    jmp %%entry
%%entry:
%if %1 == 3
    mov eax,3
%else
    mov eax,0x80000000
%endif
    mov ecx,%2
%if %1 == 3
    shr eax,cl
%else
    shl eax,cl
%endif
    jmp %%read
%%read:
    setc dl
    check dl,%3
%endmacro
; Alternate actual flag classes at a hot destination. Class-dependent
; targets must miss an ANY-only probe; targets replacing flags may hit it.
%macro shared_case 2
    inc ebp
    xor eax,eax
    sub al,1
    jmp %%entry
%%entry:
    mov eax,0x80000000
    mov ecx,%1
    shl eax,cl
    mov ebx,%%returned
    jmp %2
%%returned:
    check dl,1
%endmacro
; Cached native FP values and guest flags must be published before an
; unknown-class cache hit enters a class-independent destination.
%macro fp_unknown 1
    inc ebp
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    fninit
    mov word [0x6300],%1
    fldcw [0x6300]
    fld1
    fadd dword [one]
    mov eax,0x80000000
    sub al,1
    mov ecx,1
    shl eax,cl
    jmp %%target
%%target:
    fadd dword [one]
    fstp qword [0x6310]
    pushfd
    pop edx
    and edx,0x8c5 ; AF is undefined after shifts
    check edx,0x801
    check eax,0x1fe
    check dword [0x6310],0
    check dword [0x6314],0x40080000
%endmacro
start:
    rep_test_init
    mov edi,2
.again:
    carry_case 0,1
    carry_case 1,1
    carry_case 2,0
    carry_case 31,0
    relation_case 0
    relation_case 1
    sahf_case 0,0
    sahf_case 1,1
    preserved_case 3,0,1
    preserved_case 3,1,1
    preserved_case 3,2,1
    preserved_case 3,31,0
    preserved_case 0,0,1
    preserved_case 0,1,1
    preserved_case 0,2,0
    preserved_case 0,31,0
    shared_case 0,shared_cf
    shared_case 1,shared_cf
    shared_case 0,shared_own
    shared_case 1,shared_own
    fp_unknown 0x7f
    fp_unknown 0x37f
    dec edi
    jnz .again
    check ebp,45
    mov dword [R],0x4b4f4945 ; EIOK
.halt:
    cli
    hlt
    jmp .halt
shared_cf:
    setc dl
    jmp ebx
shared_own:
    xor edx,edx
    inc edx
    jmp ebx
one: dd 1.0
    rep_test_handlers
times 0x100000-($-$$) db 0xff
