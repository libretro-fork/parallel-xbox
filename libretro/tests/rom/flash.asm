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
%ifndef PCM
%define PCM          0x00200000
%endif
%define GP           (APU + 0x30000)
%define GPPROG       0x00120000     ; the GP's program, 2 pages
%define GPSGE        0x00118000
%define EPMMIO       (APU + 0x50000)
%define EPPROG       0x00124000     ; the EP's program, 2 pages
%define EPSGE        0x00119000
%define EPFSGE       0x0011A000     ; the EP's FIFO pages
%define EPFIFO       0x00130000     ; its output FIFO 0, one page
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
%ifdef GPU
%include "gpu.inc"          ; GPU_BASE, GPU_ENTRY and the bytes, laid out at the end
%endif
%ifdef ACI
%include "aci.inc"          ; ACI_BASE, ACI_ENTRY and the bytes, laid out at the end
%endif
%ifdef DVD
%include "dvd.inc"          ; DVD_BASE, DVD_ENTRY and the bytes, laid out at the end
%endif
%ifdef HDD
%include "hdd.inc"          ; HDD_BASE, HDD_ENTRY and the bytes, laid out at the end
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
%ifdef STREAM
        ; A long SSL of repeated tone segments, with the same physical
        ; source address in each entry. STREAM + PCM=0 tests RAM address 0.
        mov edi, 0x00150000
        mov ecx, 255
.ssl:   mov dword [edi], PCM
        mov dword [edi + 4], (1 << 16) | 1920 ; B16, mono, one sample/block
        add edi, 8
        loop .ssl
        apu 0x2034, 0x00150000
%endif
        apu 0x1100, 0           ; FECTL free running

        ; submix headroom of bins 0 and 1
        pio 0x200, HEADROOM
        pio 0x204, HEADROOM

        pio 0x2F8, VOICE                 ; SET_CURRENT_VOICE
        pio 0x300, (1 << 5)              ; VBIN: v0 -> bin 0, v1 -> bin 1
%ifdef STREAM
        pio 0x304, (1 << 30) | (1 << 28) | (1 << 24) | (1 << 23) ; persistent stream
        pio 0x320, 255                   ; SSL A, entries 0..254
%else
        pio 0x304, (1 << 30) | (1 << 28) | (1 << 25) ; B16, S16, loop, mono
%endif
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
%ifdef VP_UNIQUE
        ; Clone the inactive descriptor, then activate 64 distinct voices.
        ; Their PCM is shared read-only; filter/resampler state is per voice.
        mov ebx, VOICE + 1
.vp_clone:
        mov esi, VOICES + VOICE * 0x80
        mov edi, ebx
        shl edi, 7
        add edi, VOICES
        mov ecx, 0x80 / 4
        rep movsd
        inc ebx
        cmp ebx, VOICE + 64
        jb .vp_clone
%endif
        pio 0x120, (1 << 16)             ; antecedent: 2D list top
        pio 0x124, VOICE                 ; VOICE_ON, envelopes off
%ifdef VP_UNIQUE
        mov ebx, VOICE + 1
.vp_activate:
        pio 0x124, ebx
        inc ebx
        cmp ebx, VOICE + 64
        jb .vp_activate
%endif
%ifdef VP_REPEAT
        ; A shared list tail must execute twice in order, never concurrently.
        apu 0x2060, VOICE                ; TVL3D shares the 2D voice
%endif
%ifdef VP_INVALID_HEAD
        ; A malformed second list must not alias or disrupt the valid 2D voice.
        apu 0x2060, VP_INVALID_HEAD
%endif
%ifdef VP_INVALID_LINK
        ; Keep the valid prefix, then stop before reading an invalid descriptor.
        mov word [VOICES + VOICE * 0x80 + 0x7C], VP_INVALID_LINK
%endif
%ifdef VP_CYCLE
        ; Each list is bounded at 256 visits: 768 total must not overflow
        ; the 256-item work queue or discard an earlier partial mix.
        mov word [VOICES + VOICE * 0x80 + 0x7C], VOICE
        apu 0x2060, VOICE
        apu 0x206C, VOICE
%endif


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
%ifdef EP
        ; the EP DSP's program (ep.a56) into its scratch memory, its output
        ; FIFO 0 one page, the DMA block it sends with at X:$0010 (512
        ; 16-bit samples from X:$0100 to FIFO 0), then the EP out of reset
        mov esi, ep_prog
        mov edi, EPPROG
        mov ecx, (ep_prog_end - ep_prog) / 4
        rep movsd
        mov dword [EPSGE], EPPROG
        mov dword [EPSGE + 4], 0
        mov dword [EPSGE + 8], EPPROG + 0x1000
        mov dword [EPSGE + 12], 0
        apu 0x2048, EPSGE                ; EPSADDR
        apu 0x20DC, 1                    ; EPSMAXSGE
        mov dword [EPFSGE], EPFIFO
        mov dword [EPFSGE + 4], 0
        apu 0x204C, EPFSGE               ; EPFADDR
        apu 0x20E0, 0                    ; EPFMAXSGE
        apu 0x4024, 0                    ; EPOFBASE0
        apu 0x4028, 0x1000               ; EPOFEND0
        apu 0x402C, 0                    ; EPOFCUR0
        mov dword [EPMMIO + 0xFFFC], 0       ; EPRST: in reset
        mov dword [EPMMIO + 0x10 * 4], 0x4000 ; next: end of list
        mov dword [EPMMIO + 0x11 * 4], 0x402 ; to FIFO 0, 16-bit
        mov dword [EPMMIO + 0x12 * 4], 512   ; samples
        mov dword [EPMMIO + 0x13 * 4], 0x100 ; from X:$0100
        mov dword [EPMMIO + 0xFFFC], 3       ; EPRST | EPDSPRST: bootstrap, run
%endif

        ; start the sound engine
        apu 0x2000, (1 << 3)             ; SECTL XCNTMODE

%ifdef EXITWORK
        ; Active register work can cross STI/CLI block boundaries without
        ; storing. Finish well inside one frame; an exit-count idle rule
        ; incorrectly skips to timers and stretches it over many frames.
        mov al, 0xff
        out 0x21, al
        out 0xa1, al
        xor eax, eax
        mov edx, 1024
.outer: mov ecx, 64
.inner: sti
        nop
        cli
        add eax, 1
        dec ecx
        jnz .inner
        mov [0x00300004], eax
        dec edx
        jnz .outer
        mov dword [0x00300000], 0x574f524b
        jmp halt
%endif

%ifdef GPWRITE
        ; with -DDSP: the CPU writes 1024 words of the GP's X memory (X:$0100
        ; on, which its program does not use) and the passes at X:$0000
        ; while the GP's frames run on the DSPs' thread, then reads the
        ; words back, round after round. The writes go into the thread's
        ; write queue when it is on a frame (gp_ep.c), the reads wait for
        ; it and take what is queued first: a word read back other than
        ; the one written puts the GP in reset, and the tone stops.
        xor ebx, ebx
gpw:    mov dword [GP + 0x0000], GP_PASSES
        xor ecx, ecx
.w:     mov eax, ebx
        shl eax, 10
        add eax, ecx
        and eax, 0xFFFFFF
        mov [GP + 0x400 + ecx * 4], eax
        inc ecx
        cmp ecx, 1024
        jne .w
        xor ecx, ecx
.r:     mov eax, ebx
        shl eax, 10
        add eax, ecx
        and eax, 0xFFFFFF
        cmp [GP + 0x400 + ecx * 4], eax
        jne .bad
        inc ecx
        cmp ecx, 1024
        jne .r
        inc ebx
        jmp gpw
.bad:   mov dword [GP + 0xFFFC], 0       ; GPRST: in reset
        jmp halt
%endif
%ifdef BUSY
        ; the CPU counts in RAM at 0x300000, for measuring guest progress
busy:   inc dword [0x00300000]
        jmp busy
%endif
%ifdef PAD
        ; the pad through the USB controller (pad.c): never returns
        call PAD_ENTRY
%endif
%ifdef GPU
        ; The RAM-wait proof temporarily enables IF, with no IRQ handler.
        ; Keep PIC lines masked as in the standalone PROVEN_WAIT fixture.
        mov al, 0xff
        out 0x21, al
        out 0xa1, al
        ; the NV2A's display and renderer scenario (gpu.c): never returns
        call GPU_ENTRY
%endif
%ifdef ACI
        ; the AC97 controller's PCM out, timed in guest time (aci.c)
        call ACI_ENTRY
%endif
%ifdef DVD
        ; DVD reads by bus-master DMA, checked word by word (dvd.c)
        call DVD_ENTRY
%endif
%ifdef HDD
        ; the hard disk's writes and reads, DMA and PIO (hdd.c)
        call HDD_ENTRY
%endif
%ifdef PROVEN_WAIT
        ; An exact supported RAM-counter proof, with interrupts enabled.
        ; PIC lines stay masked because this fixture installs no IRQ handler.
        mov al, 0xff
        out 0x21, al
        out 0xa1, al
        mov dword [0x00300004], 0
        mov edx, 0x00300004
        mov esi, 1
        xor eax, eax
        sti
        nop
        nop
ram_wait:
        ; Preserve the proof's exact encodings (NASM otherwise chooses
        ; equivalent MOV/SUB/CMP encodings that are intentionally unproved).
        db 0x8b,0x0a,0x8b,0xfe,0x2b,0xf9,0x3b,0xc7,0x72,0xf6
        jmp ram_wait
%endif
%ifdef SPIN
        ; An arbitrary store-free poll is deliberately NOT an idle proof.
spin:   cmp dword [0x00300004], 0
        jne spin
        jmp spin
%endif
halt:   hlt
        jmp halt

        align 16
%ifdef EP
ep_prog:
%include "ep.inc"
ep_prog_end:
%endif
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
%ifdef GPU
        times GPU_BASE - 0xFFF00000 - ($ - $$) db 0xFF
        gpu_bytes
%endif
%ifdef ACI
        times ACI_BASE - 0xFFF00000 - ($ - $$) db 0xFF
        aci_bytes
%endif
%ifdef DVD
        times DVD_BASE - 0xFFF00000 - ($ - $$) db 0xFF
        dvd_bytes
%endif
%ifdef HDD
        times HDD_BASE - 0xFFF00000 - ($ - $$) db 0xFF
        hdd_bytes
%endif
        times 0x100000 - ($ - $$) db 0xFF
