; SPDX-License-Identifier: GPL-2.0-or-later
; Warm same-page calls into an instruction spanning two pages, then remap
; its second page, modify each physical code page and fault on that page.
bits 32
org 0xfff00000
%include "rep-common.inc"
start:
    rep_test_init
    mov edi,0x7070
    mov eax,page_fault
    call gate
    mov esi,sender
    mov edi,0x201000
    mov ecx,sender_end-sender
    rep movsb
    mov dword [0x400000],0xc3222222
    mov edi,0x500000
    mov eax,0x83
    mov ecx,1024
.directory:
    stosd
    add eax,0x400000
    loop .directory
    mov edi,0x501000
    mov eax,3
    mov ecx,1024
.table:
    stosd
    add eax,0x1000
    loop .table
    mov dword [0x500000],0x501003
    mov eax,cr4
    or eax,0x10
    mov cr4,eax
    mov eax,0x500000
    mov cr3,eax
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax
    mov edx,0x11111111
    call run
    inc ebp
    mov dword [0x501808],0x400003
    invlpg [0x202000]
    mov edx,0x22222211
    call run
    inc ebp
    mov dword [0x400000],0xc3333333
    mov edx,0x33333311
    call run
    inc ebp
    mov byte [0x201fff],0x44
    mov edx,0x33333344
    call run
    inc ebp
    mov dword [0x501808],0
    invlpg [0x202000]
    call run
    check dword [R+40],1
    inc ebp
    mov dword [0x501808],0x202003
    mov eax,cr3
    mov cr3,eax
    mov edx,0x11111144
    call run
    mov dword [R],0x4b4f5054 ; TPOK
.halt:
    cli
    hlt
    jmp .halt
run:
    mov eax,0x201000
    call eax
    cmp dword [R+4],0
    jne start.halt
    ret
page_fault:
    inc dword [R+40]
    mov dword [0x501808],0x400003
    invlpg [0x202000]
    add esp,4
    iretd
sender:
    mov ecx,256
.again:
    ; Alternate incoming classes through the shared call site. ADC in the
    ; crossing target makes each translation depend on its incoming carry.
    test ecx,1
    jz .logic
    mov ebx,0
    add ebx,1
    jmp .call
.logic:
    or ebx,0
.call:
    mov eax,0
    call .cross
    cmp eax,edx
    jne .bad
    loop .again
    ret
.bad:
    mov [R+4],ebp
    ret
    times 0xffe-($-sender) db 0x90
.cross:
    adc eax,strict dword 0x11111111
    ret
sender_end:
    rep_test_handlers
times 0x100000-($-$$) db 0xff
