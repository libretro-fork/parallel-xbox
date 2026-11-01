; Synthetic program-memory mutation regression using GP frame handshake.
; REP writes its own absolute-EA extension (8 -> $40). Its second
; iteration must decode that new extension and write P:$40.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x44F400 ; P:04
        dd 0x000040 ; P:05
        dd 0x0602A0 ; P:06
        dd 0x077084 ; P:07
        dd 0x000008 ; P:08
        dd 0x08F484 ; P:09
        dd 0x000001 ; P:0A
        dd 0x0AF080 ; P:0B
        dd 0x000000 ; P:0C
