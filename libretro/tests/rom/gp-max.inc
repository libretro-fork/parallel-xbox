; GP frame fixture using the same handshake and loops as gp.inc.
; REP #64 / MAX A,B checks repeated transfers at frame entry.
; ADD X0,B followed immediately by MAX A,B must preserve deferred
; E/U/N/Z flags even when it overwrites B with A (initially zero).
; MOVE B,X:(R0)+ observes the resulting accumulator after each sample.
        dd 0x0A8581
        dd 0x000000
        dd 0x08F485
        dd 0x000002
        dd 0x0640A0
        dd 0x20001D
        dd 0x060000
        dd 0x000011
        dd 0x60F400
        dd 0x001400
        dd 0x064080
        dd 0x000010
        dd 0x44E000
        dd 0x200048
        dd 0x20001D
        dd 0x000000
        dd 0x575800
        dd 0x000000
        dd 0x08F484
        dd 0x000001
        dd 0x0AF080
        dd 0x000000
