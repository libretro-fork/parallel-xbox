; SPDX-License-Identifier: GPL-2.0-or-later
; Immediate-zero SUB/CMP flags, register preservation and carry controls.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro run_case 2
 mov eax,ebx
 mov edx,ebx
 and edx,0x8d5
 or edx,2
 push edx
 popfd
 call %1
 check eax,ebx
 test %2,%2
 pushfd
 pop edi
 and edi,0x8d5
 and edx,0x8d5
 check edx,edi
 inc ebp
%endmacro
start:
 rep_test_init
 mov esp,0x3ff000
 mov edi,0x7030
 mov eax,ud_handler
 call gate
 mov esi,seeds
 mov ecx,256
.again:
 lodsd
 mov ebx,eax
 run_case k_sub_al,al
 run_case k_sub_ah,ah
 run_case k_sub_ax,ax
 run_case k_sub_eax,eax
 run_case k_cmp_al,al
 run_case k_cmp_ah,ah
 run_case k_cmp_ax,ax
 run_case k_cmp_eax,eax
 dec ecx
 jnz .again
 check ebp,2049
 mov eax,0x80000000
 stc
 sbb eax,0
 check eax,0x7fffffff
 mov eax,0x12345600
 stc
 sbb al,0
 check eax,0x123456ff
 mov eax,0x12340000
 stc
 sbb ax,0
 check eax,0x1234ffff
 mov eax,0
 clc
 sbb eax,0
 check eax,0
 ; Same address-base/source register does not make memory operands identical.
 mov eax,R+128
 lea edx,[eax+90]
 mov [eax],edx
 sub [eax],eax
 pushfd
 pop edx
 check dword [eax],90
 and edx,0x8d5
 check edx,4
 lea edx,[eax+90]
 mov [eax],edx
 cmp [eax],eax
 pushfd
 pop edx
 and edx,0x8d5
 check edx,4
 lea edx,[eax+90]
 check [eax],edx
 sub eax,[eax]
 pushfd
 pop edx
 check eax,-90
 and edx,0x8d5
 check edx,0x95
 ; Invalid LOCK register forms still #UD.
 mov dword [R+16],locked
 mov dword [R+20],after_locked
locked: db 0xf0,0x83,0xe8,0
after_locked:
 check dword [R+12],1
 ; Inaccessible memory forms still fault and preserve the register/CF.
 mov edi,0x500000
 xor eax,eax
 mov ecx,1024
 rep stosd
 mov dword [0x500000],0x83
 mov dword [0x500ffc],0xffc00083
 mov eax,cr4
 or eax,0x10
 mov cr4,eax
 mov eax,0x500000
 mov cr3,eax
 mov eax,cr0
 or eax,0x80000000
 mov cr0,eax
 mov eax,0x1000000
 mov dword [R+16],bad_sub
 mov dword [R+20],after_bad_sub
 stc
bad_sub: lock sub dword [eax],0
after_bad_sub:
 jnc fail
 check eax,0x1000000
 check dword [R+24],1
 mov dword [R+16],bad_cmp
 mov dword [R+20],after_bad_cmp
 stc
bad_cmp: cmp dword [eax],0
after_bad_cmp:
 jnc fail
 check eax,0x1000000
 check dword [R+24],2
 mov dword [R+8],2048
 mov dword [R],0x53535542
.halt:
 cli
 hlt
 jmp .halt
fail:
 mov [R+4],ebp
 mov dword [R],0x4641494c
 jmp after_bad_cmp.halt
gate:
 mov word [edi],ax
 mov word [edi+2],8
 mov word [edi+4],0x8e00
 shr eax,16
 mov word [edi+6],ax
 ret
ud_handler:
 push eax
 mov eax,[esp+4]
 check eax,[R+16]
 mov eax,[R+20]
 mov [esp+4],eax
 inc dword [R+12]
 pop eax
 iretd
fault_handler:
 push eax
 mov eax,cr2
 check eax,0x1000000
 mov eax,[esp+8]
 check eax,[R+16]
 mov eax,[R+20]
 mov [esp+8],eax
 inc dword [R+24]
 pop eax
 add esp,4
 iretd
debug_handler: jmp fail
irq_handler: jmp fail
idtr: dw 2047
 dd 0x7000
times 0x8000-($-$$) db 0x90
k_sub_al:
 sub al,0
 pushfd
 pop edx
 ret
times 0x8100-($-$$) db 0x90
k_sub_ah:
 sub ah,0
 pushfd
 pop edx
 ret
times 0x8200-($-$$) db 0x90
k_sub_ax:
 sub ax,0
 pushfd
 pop edx
 ret
times 0x8300-($-$$) db 0x90
k_sub_eax:
 sub eax,0
 pushfd
 pop edx
 ret
times 0x8400-($-$$) db 0x90
k_cmp_al:
 cmp al,0
 pushfd
 pop edx
 ret
times 0x8500-($-$$) db 0x90
k_cmp_ah:
 cmp ah,0
 pushfd
 pop edx
 ret
times 0x8600-($-$$) db 0x90
k_cmp_ax:
 cmp ax,0
 pushfd
 pop edx
 ret
times 0x8700-($-$$) db 0x90
k_cmp_eax:
 cmp eax,0
 pushfd
 pop edx
 ret
times 0x9000-($-$$) db 0x90
seeds:
 dd 0,0xffffffff,0x80000000,0x7fffffff
%assign seed 0x9e3779b9
%rep 252
 %assign seed ((seed*1664525+1013904223) & 0xffffffff)
 dd seed
%endrep
times 0x100000-($-$$) db 0xff
