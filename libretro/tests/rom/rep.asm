; SPDX-License-Identifier: GPL-2.0-or-later
; REP MOVS regression ROM. Entered by bootrom.asm in flat protected mode.
; No Xbox firmware, disk or graphics setup required. Results at 0x300000.
bits 32
org 0xfff00000
%define R 0x300000
%macro check 2
    cmp %1,%2
    jne fail
%endmacro
start:
    cli
    cld
    mov edi,R
    xor eax,eax
    mov ecx,16
    rep stosd
    mov ebp,1
    ; Install #DB, #PF and IRQ0 gates in an otherwise empty IDT.
    mov edi,0x7000
    mov ecx,512
    rep stosd
    mov edi,0x7008
    mov eax,debug_handler
    call gate
    mov edi,0x7070
    mov eax,fault_handler
    call gate
    mov edi,0x7100
    mov eax,irq_handler
    call gate
    lidt [idtr]
    ; Dword copies crossing both source and destination page boundaries.
    mov edi,0x40fff8
    mov ecx,2050
    mov eax,0x5a000000
.fill:
    stosd
    inc eax
    loop .fill
    mov esi,0x40fff8
    mov edi,0x420ffc
    mov ecx,2050
    rep movsd
    check ecx,0
    check esi,0x412000
    check edi,0x423004
    mov edi,0x420ffc
    mov ecx,2050
    mov eax,0x5a000000
.verify:
    check [edi],eax
    add edi,4
    inc eax
    loop .verify
    ; Word copy backwards, without changing its source or oracle.
    inc ebp
    std
    mov esi,0x411ffe
    mov edi,0x424ffe
    mov ecx,4100
    rep movsw
    cld
    check ecx,0
    check esi,0x40fff6
    check edi,0x422ff6
    mov esi,0x40fff8
    mov edi,0x422ff8
    mov ecx,2050
.verify_back:
    mov eax,[esi]
    check [edi],eax
    add esi,4
    add edi,4
    loop .verify_back
    ; Forward overlap must propagate the first byte, unlike memmove.
    inc ebp
    mov byte [0x430000],0xa5
    mov esi,0x430000
    mov edi,0x430001
    mov ecx,257
    rep movsb
    mov esi,0x430000
    mov ecx,258
.overlap_f:
    check byte [esi],0xa5
    inc esi
    loop .overlap_f
    ; Backward overlap propagates the final byte.
    inc ebp
    mov byte [0x430101],0x5a
    std
    mov esi,0x430101
    mov edi,0x430100
    mov ecx,257
    rep movsb
    cld
    mov esi,0x430000
    mov ecx,258
.overlap_b:
    check byte [esi],0x5a
    inc esi
    loop .overlap_b
    ; Zero count cannot touch memory or update either pointer.
    inc ebp
    mov esi,0xdeadbeef
    mov edi,0xbaadf00d
    xor ecx,ecx
    rep movsd
    check esi,0xdeadbeef
    check edi,0xbaadf00d
    ; 16-bit addressing: only CX/SI/DI participate; upper halves survive.
    inc ebp
    mov dword [0x4000],0x12345678
    mov esi,0x12344000
    mov edi,0x56788000
    mov ecx,0xabcd0004
    a16 rep movsb
    check esi,0x12344004
    check edi,0x56788004
    check ecx,0xabcd0000
    check dword [0x8000],0x12345678
    inc ebp
    mov word [0xfffe],0xf1f0
    mov word [0],0xf3f2
    mov esi,0x1234fffe
    mov edi,0x56788000
    mov ecx,0xabcd0004
    a16 rep movsb
    check esi,0x12340002
    check edi,0x56788004
    check ecx,0xabcd0000
    check dword [0x8000],0xf3f2f1f0
    ; TF traps after each iteration, including the final one.
    inc ebp
    mov esi,0x4000
    mov edi,0x8000
    mov ecx,5
    pushfd
    or dword [esp],0x100
    popfd
    rep movsb
    check dword [R+8],5
    ; RF remains set throughout REP, then permits the following breakpoint.
    inc ebp
    mov eax,.after_rf
    mov dr0,eax
    mov eax,0x401
    mov dr7,eax
    mov esi,0x4000
    mov edi,0x8000
    mov ecx,32
    pushfd
    or dword [esp],0x10000
    push dword 8
    push dword .rep_rf
    iretd
.rep_rf:
    rep movsb
.after_rf:
    nop
    check dword [R+12],1
    ; A data watchpoint traps after its element, with updated REP count.
    inc ebp
    mov dword [0x4000],0x01020304
    mov dword [0x4004],0x05060708
    mov eax,0x8002
    mov dr1,eax
    mov eax,0x00100404           ; local DR1, one-byte write watchpoint
    mov dr7,eax
    mov esi,0x4000
    mov edi,0x8000
    mov ecx,8
    rep movsb
    check dword [R+32],1
    check dword [R+36],5
    check dword [0x8000],0x01020304
    check dword [0x8004],0x05060708
    ; PIT interrupts must be delivered partway through a long copy.
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
    mov al,0x54                 ; divisor 596, about half a millisecond
    out 0x40,al
    mov al,2
    out 0x40,al
    ; Let IRQ0 become pending while IF is clear. STI's shadow must cover
    ; the first REP iteration, then deliver it with count - 1 remaining.
    mov al,0x0a
    out 0x20,al
.pending:
    in al,0x20
    test al,1
    jz .pending
    mov esi,0x800000
    mov edi,0xa00000
    mov ecx,0x100000
    sti
    rep movsb
    cli
    cmp dword [R+16],0
    je fail
    check dword [R+20],0xfffff
    ; Copy into the page currently executing REP. Invalidating that code
    ; must leave its remaining count/pointers correct and discard the old
    ; target function's translation, even when the first byte is unchanged.
    inc ebp
    mov dword [0x600000],0x000001b8
    mov word [0x600004],0xc300
    mov dword [0x600100],0x00c3a4f3  ; rep movsb; ret
    mov eax,0x600000
    call eax
    check eax,1
    mov dword [0x4000],0x000002b8
    mov word [0x4004],0xc300
    mov esi,0x4000
    mov edi,0x600000
    mov ecx,6
    mov eax,0x600100
    call eax
    check ecx,0
    check esi,0x4006
    check edi,0x600006
    mov eax,0x600000
    call eax
    check eax,2
    ; The REP opcode itself crosses a code-page boundary. Its two-page
    ; block must remain protected by the existing chaining restrictions.
    inc ebp
    mov byte [0x600fff],0xf3
    mov word [0x601000],0xc3a4
    mov esi,0x4000
    mov edi,0x8000
    mov ecx,6
    mov eax,0x600fff
    call eax
    check ecx,0
    check esi,0x4006
    check edi,0x8006
    check dword [0x8000],0x000002b8
    check word [0x8004],0xc300
    ; A fault halfway through a copy must restart at the remaining byte.
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
    mov dword [0x800000],0x12345678
    mov dword [0x800004],0xabcdef01
    mov dword [0x800008],0x24681357
    mov dword [0x80000c],0xdeadbeef
    mov esi,0x800000
    mov edi,0xfffff8
    mov ecx,16
    rep movsb
    check ecx,0
    check dword [R+24],1
    check dword [R+28],8
    mov esi,0x800000
    mov edi,0xfffff8
    mov ecx,4
.verify_fault:
    mov eax,[esi]
    check [edi],eax
    add esi,4
    add edi,4
    loop .verify_fault
    mov dword [R],0x5245504f
.halt:
    cli
    hlt
    jmp .halt
fail:
    mov [R+4],ebp
    jmp start.halt
gate:
    mov word [edi],ax
    mov word [edi+2],8
    mov word [edi+4],0x8e00
    shr eax,16
    mov word [edi+6],ax
    ret
debug_handler:
    push eax
    mov eax,dr6
    test eax,2
    jnz .data
    test eax,1
    jnz .hardware
    inc dword [R+8]
    test ecx,ecx
    jnz .return
    and dword [esp+12],~0x100
    jmp .return
.hardware:
    inc dword [R+12]
    xor eax,eax
    mov dr7,eax
    jmp .return
.data:
    inc dword [R+32]
    mov [R+36],ecx
    xor eax,eax
    mov dr7,eax
.return:
    xor eax,eax
    mov dr6,eax
    pop eax
    iretd
irq_handler:
    push eax
    cmp dword [R+16],0
    jne .ack
    mov [R+20],ecx
.ack:
    inc dword [R+16]
    mov al,0x20
    out 0x20,al
    pop eax
    iretd
fault_handler:
    inc dword [R+24]
    mov [R+28],ecx
    mov dword [0x500010],0x1000083
    invlpg [0x1000000]
    add esp,4
    iretd
idtr:
    dw 2047
    dd 0x7000
times 0x100000-($-$$) db 0xff
