; LRA current-PC addressing, aliases, REP and preserved deferred flags.
        dd 0x0A8581 ; P:00 frame handshake
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x67F400 ; P:04 MOVE #$1400,R7
        dd 0x001400 ; P:05
        dd 0x044050 ; P:06 LRA +$100,R0 at current PC
        dd 0x000100 ; P:07
        dd 0x04C018 ; P:08 LRA R0,N0
        dd 0x04404E ; P:09 LRA -1,A
        dd 0xFFFFFF ; P:0A
        dd 0x565F00 ; P:0B observe A
        dd 0x04C00E ; P:0C LRA R0,A
        dd 0x565F00 ; P:0D observe A
        dd 0x0602A0 ; P:0E REP #2
        dd 0x04C010 ; P:0F LRA R0,R0; aliased source/destination
        dd 0x044044 ; P:10 LRA long,X0
        dd 0x800000 ; P:11
        dd 0x200013 ; P:12 CLR A
        dd 0x200040 ; P:13 ADD X0,A; deferred flags
        dd 0x04404E ; P:14 LRA +1,A must retain earlier flags
        dd 0x000001 ; P:15
        dd 0x0445B9 ; P:16 MOVEC SR,X1
        dd 0x565F00 ; P:17 observe A
        dd 0x08F484 ; P:18 halt until next frame
        dd 0x000001 ; P:19
        dd 0x0AF080 ; P:1A restart
        dd 0x000000 ; P:1B
