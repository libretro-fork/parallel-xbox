; SPDX-License-Identifier: GPL-2.0-or-later
; Unsupported crypto must trap before operand access and preserve state.
bits 32
org 0xfff00000
%define R 0x300000
%macro check 2
 cmp %1,%2
 jne fail
%endmacro
%macro trap 1+
 mov dword [R+16],%%op
 mov dword [R+20],%%next
 stc
 pushfd
 pop dword [R+28]
%%op: %1
%%next:
 jnc fail
 push eax
 pushfd
 pop eax
 xor eax,[R+28]
 and eax,0x8d5
 jnz fail
 pop eax
 inc ebp
 check dword [R+8],ebp
%endmacro
start:
 cli
 cld
 mov esp,0x3ff000
 mov edi,R
 xor eax,eax
 mov ecx,64
 rep stosd
 xor ebp,ebp
 mov edi,0x7000
 mov ecx,512
 rep stosd
 mov edi,0x7030
 mov eax,ud_handler
 call gate
 mov edi,0x7038
 mov eax,fail
 call gate
 mov edi,0x7068
 mov eax,fail
 call gate
 mov edi,0x7070
 mov eax,pf_handler
 call gate
 lidt [idtr]
 mov edi,0x500000
 xor eax,eax
 mov ecx,1024
 rep stosd
 mov dword [0x500000],0x83
 mov dword [0x500ffc],0xffc00083
 mov eax,cr4
 or eax,0x610
 mov cr4,eax
 mov eax,0x500000
 mov cr3,eax
 mov eax,cr0
 and eax,~12
 or eax,0x80000002
 mov cr0,eax
 mov eax,1
 cpuid
 test edx,0x10
 jz fail
 test ecx,0x42000002
 jnz fail
 movups xmm0,[pattern]
 movups xmm1,[pattern]
 trap aesenc xmm0,xmm1
 trap aesenclast xmm0,xmm1
 trap aesdec xmm0,xmm1
 trap aesdeclast xmm0,xmm1
 trap aesimc xmm0,xmm1
 trap aeskeygenassist xmm0,xmm1,0x5a
 trap pclmulqdq xmm0,xmm1,0x11
 trap aesenc xmm0,[0x1000000]
 trap aesenclast xmm0,[0x1000000]
 trap aesdec xmm0,[0x1000000]
 trap aesdeclast xmm0,[0x1000000]
 trap aesimc xmm0,[0x1000000]
 trap aeskeygenassist xmm0,[0x1000000],0x5a
 trap pclmulqdq xmm0,[0x1000000],0x11
 trap vaesenc xmm0,xmm1,xmm2
 trap vaesenc ymm0,ymm1,[0x1000000]
 trap vpclmulqdq xmm0,xmm1,[0x1000000],0x11
 trap vaesimc xmm0,[0x1000000]
 ; Random instructions are absent too, with no destination writes.
 mov eax,0x76543210
 trap rdrand ax
 check eax,0x76543210
 trap rdrand eax
 check eax,0x76543210
 trap rdseed ax
 check eax,0x76543210
 trap rdseed eax
 check eax,0x76543210
 trap db 0x0f,0xc7,0x35,0,0,0,1 ; invalid memory RDRAND [0x1000000]
 trap db 0x0f,0xc7,0x3d,0,0,0,1 ; invalid memory RDSEED [0x1000000]
 trap db 0xf3,0x0f,0xc7,0xf0   ; invalid REP RDRAND
 trap db 0xf2,0x0f,0xc7,0xf0   ; invalid REPNZ RDRAND
 trap db 0xf3,0x0f,0xc7,0xf8   ; RDPID remains independently unsupported
 check eax,0x76543210
 ; RDPID register/size-prefix forms preserve all destination values.
 mov ebx,0x12345678
 mov ecx,0x89abcdef
 mov edx,0xfedcba98
 mov esi,0x11223344
 mov edi,0x55667788
 trap rdpid ecx
 trap rdpid edx
 trap rdpid ebx
 trap rdpid esi
 trap rdpid edi
 trap rdpid ebp
 trap db 0x66,0xf3,0x0f,0xc7,0xf8
 trap db 0xf3,0x66,0x0f,0xc7,0xf8
 trap db 0xf3,0x0f,0xc7,0x3d,0,0,0,1
 check eax,0x76543210
 check ebx,0x12345678
 check ecx,0x89abcdef
 check edx,0xfedcba98
 check esi,0x11223344
 check edi,0x55667788
 ; RDTSCP shares the old RDPID helper but is also absent from this CPU.
 trap rdtscp
 trap db 0x66,0x0f,0x01,0xf9
 trap db 0xf3,0x0f,0x01,0xf9
 trap db 0xf2,0x0f,0x01,0xf9
 trap db 0xf0,0x0f,0x01,0xf9
 check eax,0x76543210
 check ecx,0x89abcdef
 check edx,0xfedcba98
 ; Ordinary RDTSC is supported, leaves ECX/CF alone and is nondecreasing.
 stc
 rdtsc
 jnc fail
 check ecx,0x89abcdef
 push eax
 push edx
 stc
 rdtsc
 jnc fail
 cmp edx,[esp]
 jb fail
 ja clock_ok
 cmp eax,[esp+4]
 jb fail
clock_ok:
 add esp,8
 ; Preserve the supported CMPXCHG8B neighbor in the same opcode group.
 mov dword [R+128],0x11223344
 mov dword [R+132],0x55667788
 mov eax,0x11223344
 mov edx,0x55667788
 mov ebx,0xaabbccdd
 mov ecx,0xeeff0011
 lock cmpxchg8b [R+128]
 jnz fail
 check dword [R+128],0xaabbccdd
 check dword [R+132],0xeeff0011
 mov eax,cr0
 or eax,8
 mov cr0,eax
 trap aesenc xmm0,xmm1
 mov eax,cr0
 and eax,~8
 or eax,4
 mov cr0,eax
 trap aesdec xmm0,xmm1
 mov eax,cr0
 and eax,~4
 mov cr0,eax
 movups [R+64],xmm0
 movups [R+80],xmm1
 mov esi,pattern
 mov edi,R+64
 mov ecx,4
 repe cmpsd
 jne fail
 mov esi,pattern
 mov edi,R+80
 mov ecx,4
 repe cmpsd
 jne fail
 ; An incomplete opcode must fault fetching its next page, not become #UD.
 mov word [0x3ffffe],0x0f66
 mov dword [R+20],after_fetch
 mov dword [R+24],0x3ffffe
 jmp 0x3ffffe
after_fetch:
 check dword [R+12],1
 mov byte [0x3ffffd],0xf3
 mov word [0x3ffffe],0xc70f
 mov dword [R+24],0x3ffffd
 mov dword [R+20],after_rdpid_fetch
 jmp 0x3ffffd
after_rdpid_fetch:
 check dword [R+12],2
 mov word [0x3ffffe],0x010f
 mov dword [R+24],0x3ffffe
 mov dword [R+20],after_rdtscp_fetch
 jmp 0x3ffffe
after_rdtscp_fetch:
 check dword [R+12],3
 check ebp,43
 mov dword [R],0x43525950
.halt:
 cli
 hlt
 jmp .halt
fail:
 mov [R+4],ebp
 mov dword [R],0x4641494c
 jmp after_rdtscp_fetch.halt
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
 inc dword [R+8]
 pop eax
 iretd
pf_handler:
 push eax
 mov eax,cr2
 check eax,0x400000
 mov eax,[esp+8]
 check eax,[R+24]
 mov eax,[R+20]
 mov [esp+8],eax
 inc dword [R+12]
 pop eax
 add esp,4
 iretd
idtr: dw 2047
 dd 0x7000
pattern: dd 0x01234567,0x89abcdef,0x76543210,0xfedcba98
times 0x100000-($-$$) db 0xff
