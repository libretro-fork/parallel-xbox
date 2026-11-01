; Synthetic long-immediate CMP, deferred arithmetic flags and GP handshake.
; Equal CMP feeds Bcc EQ; REP and DOR repeat two-word comparisons.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x44F400 ; P:04
        dd 0x000001 ; P:05
        dd 0x200013 ; P:06
        dd 0x200040 ; P:07
        dd 0x0140C5 ; P:08
        dd 0x000001 ; P:09
        dd 0x05A402 ; P:0A
        dd 0x200013 ; P:0B
        dd 0x0603A0 ; P:0C
        dd 0x0140C5 ; P:0D
        dd 0xFFFFFF ; P:0E
        dd 0x060290 ; P:0F
        dd 0x000003 ; P:10
        dd 0x0140C5 ; P:11
        dd 0x000001 ; P:12
        dd 0x60F400 ; P:13
        dd 0x000020 ; P:14
        dd 0x565800 ; P:15
        dd 0x08F484 ; P:16
        dd 0x000001 ; P:17
        dd 0x0AF080 ; P:18
        dd 0x000000 ; P:19
