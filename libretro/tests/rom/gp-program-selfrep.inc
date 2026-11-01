; Synthetic program-memory mutation regression using GP frame handshake.
; REP replaces its own MOVEM body with CLR A; the remaining iterations
; must execute the new opcode, rather than continue the old translation.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x56F400 ; P:04
        dd 0x123456 ; P:05
        dd 0x44F400 ; P:06
        dd 0x200013 ; P:07
        dd 0x0603A0 ; P:08
        dd 0x070904 ; P:09
        dd 0x08F484 ; P:0A
        dd 0x000001 ; P:0B
        dd 0x0AF080 ; P:0C
        dd 0x000000 ; P:0D
