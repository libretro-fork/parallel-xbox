; SPDX-License-Identifier: GPL-2.0-or-later
; Exact software-FPU FCMOV: all eight conditions, all arithmetic EFLAGS,
; every TOP/source register and raw 80-bit special encodings. Run hard FPU off.
bits 32
org 0xfff00000
%include "rep-common.inc"
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
    mov ecx,128
    rep stosd
    mov word [0x4000],0x37f
    mov byte [0x4004],0xff
    mov dword [0x4018],0x1f80
    mov esi,images
    mov edi,0x4020
    mov ecx,32
    rep movsd
    xor esi,esi
.loop:
    inc ebp
    mov eax,esi
    shr eax,9
    and eax,7
    shl eax,11
    or eax,0x4500
    mov [0x4002],ax
    fxrstor [0x4000]
    mov eax,esi
    shr eax,6
    and eax,7
    mov [0x6200],eax
    mov edx,esi
    shr edx,12
    and edx,7
    mov [0x6204],edx
    shl edx,3
    add eax,edx
    mov edi,[functions+eax*4]
    mov eax,esi
    and eax,63
    mov ebx,[flags+eax*4]
    push ebx
    popfd
    call edi
    pushfd
    pop eax
    and eax,0x8d5
    mov edx,ebx
    and edx,0x8d5
    check eax,edx
    fxsave [0x5000]
    check byte [0x5004],0xff
    mov ax,[0x4002]
    check word [0x5002],ax
    mov edx,[0x6204]
    mov eax,edx
    and eax,3
    mov ecx,[masks+eax*4]
    xor eax,eax
    test ebx,ecx
    setnz al
    shr edx,2
    xor eax,edx
    test eax,eax
    jnz .selected
    mov dword [0x6200],0
.selected:
    mov edx,[0x6200]
    shl edx,4
    add edx,images
    mov eax,[edx]
    check dword [0x5020],eax
    mov eax,[edx+4]
    check dword [0x5024],eax
    mov ax,[edx+8]
    check word [0x5028],ax
    inc esi
    cmp esi,32768
    jb .loop
    check ebp,32769
    mov dword [R],0x4b4f4346 ; FCOK
.halt:
    cli
    hlt
    jmp .halt
align 4
masks: dd 1,0x40,0x41,4
flags:
%assign n 0
%rep 64
    dd 2 | ((n&1)<<0) | (((n>>1)&1)<<2) | (((n>>2)&1)<<4) | (((n>>3)&1)<<6) | (((n>>4)&1)<<7) | (((n>>5)&1)<<11)
%assign n n+1
%endrep
align 16
images:
    dq 0x8123456789abcdef
    dw 0x4000
    times 6 db 0
    dq 0
    dw 0x8000
    times 6 db 0
    dq 0x8000000000000001
    dw 0x7fff
    times 6 db 0
    dq 0xc123456789abcdef
    dw 0xffff
    times 6 db 0
    dq 0x8123456789abcdef
    dw 0x7ffe
    times 6 db 0
    dq 1
    dw 0x3fff
    times 6 db 0
    dq 1
    dw 0
    times 6 db 0
    dq 0x8123456789abcdef
    dw 1
    times 6 db 0
functions:
    dd fcmovb_0
    dd fcmovb_1
    dd fcmovb_2
    dd fcmovb_3
    dd fcmovb_4
    dd fcmovb_5
    dd fcmovb_6
    dd fcmovb_7
    dd fcmove_0
    dd fcmove_1
    dd fcmove_2
    dd fcmove_3
    dd fcmove_4
    dd fcmove_5
    dd fcmove_6
    dd fcmove_7
    dd fcmovbe_0
    dd fcmovbe_1
    dd fcmovbe_2
    dd fcmovbe_3
    dd fcmovbe_4
    dd fcmovbe_5
    dd fcmovbe_6
    dd fcmovbe_7
    dd fcmovu_0
    dd fcmovu_1
    dd fcmovu_2
    dd fcmovu_3
    dd fcmovu_4
    dd fcmovu_5
    dd fcmovu_6
    dd fcmovu_7
    dd fcmovnb_0
    dd fcmovnb_1
    dd fcmovnb_2
    dd fcmovnb_3
    dd fcmovnb_4
    dd fcmovnb_5
    dd fcmovnb_6
    dd fcmovnb_7
    dd fcmovne_0
    dd fcmovne_1
    dd fcmovne_2
    dd fcmovne_3
    dd fcmovne_4
    dd fcmovne_5
    dd fcmovne_6
    dd fcmovne_7
    dd fcmovnbe_0
    dd fcmovnbe_1
    dd fcmovnbe_2
    dd fcmovnbe_3
    dd fcmovnbe_4
    dd fcmovnbe_5
    dd fcmovnbe_6
    dd fcmovnbe_7
    dd fcmovnu_0
    dd fcmovnu_1
    dd fcmovnu_2
    dd fcmovnu_3
    dd fcmovnu_4
    dd fcmovnu_5
    dd fcmovnu_6
    dd fcmovnu_7
fcmovb_0:
    fcmovb st0,st0
    ret
fcmovb_1:
    fcmovb st0,st1
    ret
fcmovb_2:
    fcmovb st0,st2
    ret
fcmovb_3:
    fcmovb st0,st3
    ret
fcmovb_4:
    fcmovb st0,st4
    ret
fcmovb_5:
    fcmovb st0,st5
    ret
fcmovb_6:
    fcmovb st0,st6
    ret
fcmovb_7:
    fcmovb st0,st7
    ret
fcmove_0:
    fcmove st0,st0
    ret
fcmove_1:
    fcmove st0,st1
    ret
fcmove_2:
    fcmove st0,st2
    ret
fcmove_3:
    fcmove st0,st3
    ret
fcmove_4:
    fcmove st0,st4
    ret
fcmove_5:
    fcmove st0,st5
    ret
fcmove_6:
    fcmove st0,st6
    ret
fcmove_7:
    fcmove st0,st7
    ret
fcmovbe_0:
    fcmovbe st0,st0
    ret
fcmovbe_1:
    fcmovbe st0,st1
    ret
fcmovbe_2:
    fcmovbe st0,st2
    ret
fcmovbe_3:
    fcmovbe st0,st3
    ret
fcmovbe_4:
    fcmovbe st0,st4
    ret
fcmovbe_5:
    fcmovbe st0,st5
    ret
fcmovbe_6:
    fcmovbe st0,st6
    ret
fcmovbe_7:
    fcmovbe st0,st7
    ret
fcmovu_0:
    fcmovu st0,st0
    ret
fcmovu_1:
    fcmovu st0,st1
    ret
fcmovu_2:
    fcmovu st0,st2
    ret
fcmovu_3:
    fcmovu st0,st3
    ret
fcmovu_4:
    fcmovu st0,st4
    ret
fcmovu_5:
    fcmovu st0,st5
    ret
fcmovu_6:
    fcmovu st0,st6
    ret
fcmovu_7:
    fcmovu st0,st7
    ret
fcmovnb_0:
    fcmovnb st0,st0
    ret
fcmovnb_1:
    fcmovnb st0,st1
    ret
fcmovnb_2:
    fcmovnb st0,st2
    ret
fcmovnb_3:
    fcmovnb st0,st3
    ret
fcmovnb_4:
    fcmovnb st0,st4
    ret
fcmovnb_5:
    fcmovnb st0,st5
    ret
fcmovnb_6:
    fcmovnb st0,st6
    ret
fcmovnb_7:
    fcmovnb st0,st7
    ret
fcmovne_0:
    fcmovne st0,st0
    ret
fcmovne_1:
    fcmovne st0,st1
    ret
fcmovne_2:
    fcmovne st0,st2
    ret
fcmovne_3:
    fcmovne st0,st3
    ret
fcmovne_4:
    fcmovne st0,st4
    ret
fcmovne_5:
    fcmovne st0,st5
    ret
fcmovne_6:
    fcmovne st0,st6
    ret
fcmovne_7:
    fcmovne st0,st7
    ret
fcmovnbe_0:
    fcmovnbe st0,st0
    ret
fcmovnbe_1:
    fcmovnbe st0,st1
    ret
fcmovnbe_2:
    fcmovnbe st0,st2
    ret
fcmovnbe_3:
    fcmovnbe st0,st3
    ret
fcmovnbe_4:
    fcmovnbe st0,st4
    ret
fcmovnbe_5:
    fcmovnbe st0,st5
    ret
fcmovnbe_6:
    fcmovnbe st0,st6
    ret
fcmovnbe_7:
    fcmovnbe st0,st7
    ret
fcmovnu_0:
    fcmovnu st0,st0
    ret
fcmovnu_1:
    fcmovnu st0,st1
    ret
fcmovnu_2:
    fcmovnu st0,st2
    ret
fcmovnu_3:
    fcmovnu st0,st3
    ret
fcmovnu_4:
    fcmovnu st0,st4
    ret
fcmovnu_5:
    fcmovnu st0,st5
    ret
fcmovnu_6:
    fcmovnu st0,st6
    ret
fcmovnu_7:
    fcmovnu st0,st7
    ret
    rep_test_handlers
times 0x100000-($-$$) db 0xff
