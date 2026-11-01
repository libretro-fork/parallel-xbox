; Synthetic program-memory mutation regression using GP frame handshake.
; Write next opcode INC A -> CLR B, then REP three postincrementing
; writes to P:$20..$22. Read P:$20 into B1 and publish it in mixbin 0.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x44F400 ; P:04
        dd 0x20001B ; P:05
        dd 0x070704 ; P:06
        dd 0x000008 ; P:07
        dd 0x65F400 ; P:08
        dd 0x000020 ; P:09
        dd 0x44F400 ; P:0A
        dd 0x000003 ; P:0B
        dd 0x0603A0 ; P:0C
        dd 0x075D84 ; P:0D
        dd 0x07A00D ; P:0E
        dd 0x60F400 ; P:0F
        dd 0x001400 ; P:10
        dd 0x575800 ; P:11
        dd 0x08F484 ; P:12
        dd 0x000001 ; P:13
        dd 0x0AF080 ; P:14
        dd 0x000000 ; P:15
