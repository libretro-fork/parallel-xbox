; Immediate logical right shifts, zero-count flags and REP.
        dd 0x0A8581 ; P:00 frame handshake
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x67F400 ; P:04 MOVE #$1400,R7
        dd 0x001400 ; P:05
        dd 0x200013 ; P:06 CLR A
        dd 0x017F82 ; P:07 OR #63,A
        dd 0x0C1EC2 ; P:08 LSR #1,A
        dd 0x565F00 ; P:09 observe A
        dd 0x0C1EC0 ; P:0A LSR #0,A clears carry
        dd 0x565F00 ; P:0B observe unchanged word
        dd 0x20001B ; P:0C CLR B
        dd 0x0140CA ; P:0D OR #$800001,B
        dd 0x800001 ; P:0E
        dd 0x0C1E81 ; P:0F LSL #0,B updates flags
        dd 0x0602A0 ; P:10 REP #2
        dd 0x0C1EC3 ; P:11 LSR #1,B
        dd 0x575F00 ; P:12 observe B
        dd 0x0C1EF1 ; P:13 LSR #24,B
        dd 0x575F00 ; P:14 observe zero
        dd 0x0C1EFF ; P:15 LSR #31,B; defined overshift
        dd 0x08F484 ; P:16 halt until next frame
        dd 0x000001 ; P:17
        dd 0x0AF080 ; P:18 restart
        dd 0x000000 ; P:19
