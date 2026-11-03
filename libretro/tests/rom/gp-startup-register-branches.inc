; Register-relative BRA/Bcc, taken/untaken paths and deferred flags.
        dd 0x0A8581 ; P:00 frame handshake
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x60F400 ; P:04 MOVE #2,R0
        dd 0x000002 ; P:05
        dd 0x0D18C0 ; P:06 BRA R0 -> P:08
        dd 0x200013 ; P:07 skipped CLR A
        dd 0x44F400 ; P:08 MOVE #1,X0
        dd 0x000001 ; P:09
        dd 0x200013 ; P:0A CLR A
        dd 0x200040 ; P:0B ADD X0,A; deferred flags
        dd 0x61F400 ; P:0C MOVE #3,R1
        dd 0x000003 ; P:0D
        dd 0x0D1942 ; P:0E BNE R1 -> P:11
        dd 0x200013 ; P:0F skipped CLR A
        dd 0x000000 ; P:10 skipped NOP
        dd 0x0D194A ; P:11 BEQ R1 not taken
        dd 0x62F400 ; P:12 MOVE #3,R2
        dd 0x000003 ; P:13
        dd 0x0D1AC0 ; P:14 BRA R2 -> P:17
        dd 0x200013 ; P:15 skipped CLR A
        dd 0x000000 ; P:16 skipped NOP
        dd 0x67F400 ; P:17 MOVE #$1400,R7
        dd 0x001400 ; P:18
        dd 0x565F00 ; P:19 observe preserved A
        dd 0x0445B9 ; P:1A MOVEC SR,X1
        dd 0x08F484 ; P:1B halt until next frame
        dd 0x000001 ; P:1C
        dd 0x0AF080 ; P:1D restart
        dd 0x000000 ; P:1E
