; Frame handshake and nested sample/pass loops from gp.inc.
; Observe every intermediate result in X memory. LSL #0 must preserve
; the ADD's deferred flags for Tcc; long OR and short AND stay native.
; Counts 1, 24 and 31 cover ordinary shifts, final carry and overshifts.
        dd 0x0A8581 ; P:00
        dd 0x000000
        dd 0x08F485
        dd 0x000002
        dd 0x0640A0 ; REP #64
        dd 0x0C1E81 ; LSL #0,B
        dd 0x060000 ; DO X:0,pass_end
        dd 0x00001D
        dd 0x60F400 ; MOVE #$1400,R0
        dd 0x001400
        dd 0x064080 ; DO #64,sample_end
        dd 0x00001C
        dd 0x44E000 ; MOVE X:(R0),X0
        dd 0x200048 ; ADD X0,B
        dd 0x0C1E81 ; LSL #0,B
        dd 0x028048 ; Tcs X0,B
        dd 0x575800 ; MOVE B,X:(R0)+
        dd 0x0140CA ; OR #$800000,B
        dd 0x800000
        dd 0x0C1E83 ; LSL #1,B
        dd 0x017F8E ; AND #63,B
        dd 0x575800
        dd 0x0C1EB1 ; LSL #24,B
        dd 0x575800
        dd 0x0140CA ; OR #1,B
        dd 0x000001
        dd 0x0C1EBF ; LSL #31,B
        dd 0x575800
        dd 0x000000 ; sample_end
        dd 0x000000 ; pass_end
        dd 0x08F484
        dd 0x000001
        dd 0x0AF080
        dd 0x000000
