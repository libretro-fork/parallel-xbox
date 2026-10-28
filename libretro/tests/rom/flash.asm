; Test flash program: one looping 16-bit PCM voice on the MCPX APU's voice
; processor, a 1 kHz sine at 48 kHz (40 periods, 1920 samples), mixbins 0
; and 1 at 0 dB, then the CPU halts. With -DDSP the GP DSP also runs
; gp.a56 on the mixbins every engine frame. See libretro/tests/README.md.
        bits 32
        org 0xFFF00000

%define APU          0xFE800000
%define VP           (APU + 0x20000)
%define VOICES       0x00100000
%define SGE          0x00110000
%define PCM          0x00200000
%define GP           (APU + 0x30000)
%define GPPROG       0x00120000     ; the GP's program, 2 pages
%define GPSGE        0x00118000
%ifndef GP_PASSES
%define GP_PASSES    1              ; gp.a56's passes a frame
%endif
%define VOICE        64
%ifndef HEADROOM
%define HEADROOM     0
%endif

%ifdef PAD
%include "pad.inc"          ; PAD_BASE, PAD_ENTRY and the bytes, laid out at the end
%endif
%macro pcicfg 3          ; dev, reg, value (bus 0, function 0)
        mov dx, 0xCF8
        mov eax, 0x80000000 | ((%1) << 11) | (%2)
        out dx, eax
        mov dx, 0xCFC
        mov eax, %3
        out dx, eax
%endmacro
%macro apu 2
        mov dword [APU + (%1)], %2
%endmacro
%macro pio 2
        mov dword [VP + (%1)], %2
%endmacro

entry:
        ; APU: BAR0, memory decode and bus mastering
        pcicfg 5, 0x10, APU
        pcicfg 5, 0x04, 0x00000006

        ; the sine into RAM, the SGE table's one page entry
        mov esi, pcm_data
        mov edi, PCM
        mov ecx, pcm_end - pcm_data
        rep movsb
        mov dword [SGE], PCM
        mov dword [SGE + 4], 0

        ; empty voice lists, voice array, SGE table, free-running FE
        apu 0x2054, 0xFFFF      ; TVL2D
        apu 0x2060, 0xFFFF      ; TVL3D
        apu 0x206C, 0xFFFF      ; TVLMP
        apu 0x202C, VOICES      ; VPVADDR
        apu 0x2030, SGE         ; VPSGEADDR
        apu 0x2034, 0           ; VPSSLADDR
        apu 0x1100, 0           ; FECTL free running

        ; submix headroom of bins 0 and 1
        pio 0x200, HEADROOM
        pio 0x204, HEADROOM

        pio 0x2F8, VOICE                 ; SET_CURRENT_VOICE
        pio 0x300, (1 << 5)              ; VBIN: v0 -> bin 0, v1 -> bin 1
        pio 0x304, (1 << 30) | (1 << 28) | (1 << 25) ; B16, S16, loop, mono
        pio 0x308, 0                     ; ENV0
        pio 0x30C, 0                     ; ENVA
        pio 0x310, 0                     ; ENV1
        pio 0x314, 0                     ; ENVF
        pio 0x318, 0                     ; MISC: no filter
        pio 0x360, 0x000F000F            ; VOLA: vol0 = vol1 = 0 dB, 6/7 off
        pio 0x364, 0xFFFFFFFF            ; VOLB: vol2, vol3 off
        pio 0x368, 0xFFFFFFFF            ; VOLC: vol4, vol5 off
        pio 0x37C, 0                     ; pitch 0: 48 kHz
        pio 0x3A0, 0                     ; buffer base
        pio 0x3A4, 0                     ; loop start
        pio 0x3DC, 1919                  ; end
        pio 0x120, (1 << 16)             ; antecedent: 2D list top
        pio 0x124, VOICE                 ; VOICE_ON, envelopes off

%ifdef DSP
        ; the GP DSP's program (gp.a56) into its scratch memory, where its
        ; bootstrap loads P:0 from, then the GP out of reset
        mov esi, gp_prog
        mov edi, GPPROG
        mov ecx, (gp_prog_end - gp_prog) / 4
        rep movsd
        mov dword [GPSGE], GPPROG
        mov dword [GPSGE + 4], 0
        mov dword [GPSGE + 8], GPPROG + 0x1000
        mov dword [GPSGE + 12], 0
        apu 0x2040, GPSGE                ; GPSADDR
        apu 0x20D4, 1                    ; GPSMAXSGE
        mov dword [GP + 0xFFFC], 0       ; GPRST: in reset
        mov dword [GP + 0x0000], GP_PASSES ; X:$0000, the passes
        mov dword [GP + 0xFFFC], 3       ; GPRST | GPDSPRST: bootstrap, run
%endif

        ; start the sound engine
        apu 0x2000, (1 << 3)             ; SECTL XCNTMODE

%ifdef BUSY
        ; the CPU counts in RAM at 0x300000, for measuring guest progress
busy:   inc dword [0x00300000]
        jmp busy
%endif
%ifdef PAD
        ; the pad through the USB controller (pad.c): never returns
        call PAD_ENTRY
%endif
%ifdef SPIN
        ; the CPU spins on a word in RAM that nothing writes, as the kernel's
        ; idle thread does: a wait, which the core's idle detection moves
        ; the guest's time past
spin:   cmp dword [0x00300004], 0
        jne spin
        jmp spin
%endif
halt:   hlt
        jmp halt

        align 16
%ifdef DSP
gp_prog:
%include "gp.inc"
gp_prog_end:
%endif
pcm_data:
%include "sine.inc"
pcm_end:
%ifdef PAD
        times PAD_BASE - 0xFFF00000 - ($ - $$) db 0xFF
        pad_bytes
%endif
        times 0x100000 - ($ - $$) db 0xFF
