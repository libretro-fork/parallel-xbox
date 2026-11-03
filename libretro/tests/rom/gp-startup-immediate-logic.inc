; Short OR, short/long EOR, A/B, and repeated XOR cancellation.
        dd 0x0A8581 ; P:00 frame handshake
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x67F400 ; P:04 MOVE #$1400,R7
        dd 0x001400 ; P:05
        dd 0x200013 ; P:06 CLR A
        dd 0x017F82 ; P:07 OR #63,A
        dd 0x565F00 ; P:08 observe A
        dd 0x017F83 ; P:09 EOR #63,A
        dd 0x565F00 ; P:0A observe zero
        dd 0x0140C3 ; P:0B EOR #$800000,A
        dd 0x800000 ; P:0C
        dd 0x565F00 ; P:0D observe negative
        dd 0x0602A0 ; P:0E REP #2
        dd 0x0140C3 ; P:0F EOR #$ffffff,A; even repetition cancels
        dd 0xFFFFFF ; P:10
        dd 0x565F00 ; P:11 observe unchanged
        dd 0x20001B ; P:12 CLR B
        dd 0x01608A ; P:13 OR #32,B
        dd 0x0602A0 ; P:14 REP #2
        dd 0x01608B ; P:15 EOR #32,B; even repetition cancels
        dd 0x575F00 ; P:16 observe B
        dd 0x0140CB ; P:17 EOR #$800020,B
        dd 0x800020 ; P:18
        dd 0x575F00 ; P:19 observe negative B
        dd 0x08F484 ; P:1A halt until next frame
        dd 0x000001 ; P:1B
        dd 0x0AF080 ; P:1C restart
        dd 0x000000 ; P:1D
