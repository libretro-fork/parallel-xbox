; Frame handshake and nested sample/pass loops from gp.inc.
; REP exercises conditional X0 -> B; ADD leaves deferred flags for Tcc.
; Taken and untaken accumulator/paired Rn moves are followed by an SR
; read and ordinary modifier-register MOVECs before B reaches the output.
        dd 0x0A8581 ; P:00
        dd 0x000000
        dd 0x08F485
        dd 0x000002
        dd 0x0640A0 ; REP #64
        dd 0x028048 ; Tcs X0,B
        dd 0x060000 ; DO X:0,pass_end
        dd 0x000015
        dd 0x60F400 ; MOVE #$1400,R0
        dd 0x001400
        dd 0x064080 ; DO #64,sample_end
        dd 0x000014
        dd 0x44E000 ; MOVE X:(R0),X0
        dd 0x200048 ; ADD X0,B
        dd 0x033203 ; Tpl B,A R2,R3
        dd 0x02B008 ; Tmi A,B
        dd 0x0445B9 ; MOVEC SR,X1
        dd 0x04D1A1 ; MOVEC R1,M1
        dd 0x0452A1 ; MOVEC M1,R2
        dd 0x575800 ; MOVE B,X:(R0)+
        dd 0x000000 ; sample_end
        dd 0x000000 ; pass_end
        dd 0x08F484
        dd 0x000001
        dd 0x0AF080
        dd 0x000000
