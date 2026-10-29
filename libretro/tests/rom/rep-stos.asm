; SPDX-License-Identifier: GPL-2.0-or-later
; REP STOS fills: same exception/interrupt contracts as the MOVS ROM.
bits 32
org 0xfff00000
%include "rep-common.inc"
start:
    rep_test_init
    mov esi,0xdeadbeef          ; STOS must never read/update the source index
    mov edi,0x420ffc
    mov ecx,2050
    mov eax,0xdeadbeef
    rep stosd
    check ecx,0
    check edi,0x423004
    check eax,0xdeadbeef
    mov edi,0x420ffc
    mov ecx,2050
.dwords:
    check dword [edi],0xdeadbeef
    add edi,4
    loop .dwords
    inc ebp
    std
    mov edi,0x424ffe
    mov ecx,4100
    mov eax,0xabcd1234
    rep stosw
    cld
    check ecx,0
    check edi,0x422ff6
    check eax,0xabcd1234
    mov edi,0x422ff8
    mov ecx,4100
.words:
    check word [edi],0x1234
    add edi,2
    loop .words
    inc ebp
    mov byte [0x42ffff],0x77
    mov byte [0x430102],0x88
    std
    mov edi,0x430101
    mov ecx,258
    mov eax,0x123456a5
    rep stosb
    cld
    check edi,0x42ffff
    check eax,0x123456a5
    check byte [0x42ffff],0x77
    check byte [0x430102],0x88
    mov edi,0x430000
    mov ecx,258
.bytes:
    check byte [edi],0xa5
    inc edi
    loop .bytes
    inc ebp
    mov edi,0xbaadf00d
    xor ecx,ecx
    rep stosd
    check edi,0xbaadf00d
    check eax,0x123456a5
    inc ebp
    mov edi,0x5678fffe
    mov ecx,0xabcd0004
    mov eax,0x1234565a
    a16 rep stosb
    check edi,0x56780002
    check ecx,0xabcd0000
    check eax,0x1234565a
    check word [0xfffe],0x5a5a
    check word [0],0x5a5a
    ; Single stepping and RF, then a data watchpoint on the third store.
    inc ebp
    mov edi,0x8000
    mov ecx,5
    mov eax,0x1234563c
    pushfd
    or dword [esp],0x100
    popfd
    rep stosb
    check dword [R+8],5
    check eax,0x1234563c
    inc ebp
    mov eax,.after_rf
    mov dr0,eax
    mov eax,0x401
    mov dr7,eax
    mov edi,0x8000
    mov ecx,32
    mov eax,0x1234565a
    pushfd
    or dword [esp],0x10000
    push dword 8
    push dword .rep_rf
    iretd
.rep_rf:
    rep stosb
.after_rf:
    nop
    check dword [R+12],1
    check eax,0x1234565a
    inc ebp
    mov eax,0x8002
    mov dr1,eax
    mov eax,0x00100404
    mov dr7,eax
    mov edi,0x8000
    mov ecx,8
    mov eax,0x87654312
    rep stosb
    check dword [R+32],1
    check dword [R+36],5
    check eax,0x87654312
    check dword [0x8000],0x12121212
    check dword [0x8004],0x12121212
    ; Pending PIT IRQ after STI's first iteration, preserving the fill value.
    inc ebp
    mov al,0x11
    out 0x20,al
    out 0xa0,al
    mov al,0x20
    out 0x21,al
    mov al,0x28
    out 0xa1,al
    mov al,4
    out 0x21,al
    mov al,2
    out 0xa1,al
    mov al,1
    out 0x21,al
    out 0xa1,al
    mov al,0xfe
    out 0x21,al
    mov al,0xff
    out 0xa1,al
    mov al,0x34
    out 0x43,al
    mov al,0x54
    out 0x40,al
    mov al,2
    out 0x40,al
    mov al,0x0a
    out 0x20,al
.pending:
    in al,0x20
    test al,1
    jz .pending
    mov edi,0xa00000
    mov ecx,0x100000
    mov eax,0xabcdefa5
    sti
    rep stosb
    cli
    check eax,0xabcdefa5
    check dword [R+20],0xfffff
    check dword [0xa00000],0xa5a5a5a5
    check dword [0xaffffc],0xa5a5a5a5
    ; Store into the page currently executing REP, changing cached code.
    inc ebp
    mov dword [0x600000],0x000001b8
    mov word [0x600004],0xc300
    mov dword [0x600100],0x00c3aaf3  ; rep stosb; ret
    mov edx,0x600000
    call edx
    check eax,1
    mov edi,0x600000
    mov ecx,2
    mov eax,0x123456b8           ; first byte unchanged, second changes code
    mov edx,0x600100
    call edx
    check ecx,0
    check edi,0x600002
    check eax,0x123456b8
    mov edx,0x600000
    call edx
    check eax,0xb8
    inc ebp
    mov byte [0x600fff],0xf3
    mov word [0x601000],0xc3aa
    mov edi,0x8000
    mov ecx,6
    mov eax,0x112233ab
    mov edx,0x600fff
    call edx
    check ecx,0
    check edi,0x8006
    check eax,0x112233ab
    check dword [0x8000],0xabababab
    check word [0x8004],0xabab
    check word [0x8006],0x1212
    ; Fault on the ninth store: handler maps the page and resumes at byte 8.
    inc ebp
    mov edi,0x500000
    xor eax,eax
    mov ecx,1024
    rep stosd
    mov dword [0x500000],0x83
    mov dword [0x500004],0x400083
    mov dword [0x500008],0x800083
    mov dword [0x50000c],0xc00083
    mov dword [0x500ffc],0xffc00083
    mov eax,cr4
    or eax,0x10
    mov cr4,eax
    mov eax,0x500000
    mov cr3,eax
    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax
    mov edi,0xfffff8
    mov ecx,16
    mov eax,0x123456a5
    rep stosb
    check ecx,0
    check eax,0x123456a5
    check esi,0xdeadbeef
    check dword [R+24],1
    check dword [R+28],8
    mov edi,0xfffff8
    mov ecx,4
.fault_words:
    check dword [edi],0xa5a5a5a5
    add edi,4
    loop .fault_words
    mov dword [R],0x5245504f
.halt:
    cli
    hlt
    jmp .halt
    rep_test_handlers
times 0x100000-($-$$) db 0xff
