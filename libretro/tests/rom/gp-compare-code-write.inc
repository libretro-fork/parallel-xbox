; Synthetic long-immediate CMP, deferred arithmetic flags and GP handshake.
; A two-pass DOR rewrites the cached CMP extension from 0 to -1.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x44F400 ; P:04
        dd 0x000001 ; P:05
        dd 0x200013 ; P:06
        dd 0x200040 ; P:07
        dd 0x44F400 ; P:08
        dd 0xFFFFFF ; P:09
        dd 0x060290 ; P:0A
        dd 0x000005 ; P:0B
        dd 0x0140C5 ; P:0C
        dd 0x000000 ; P:0D
        dd 0x070D04 ; P:0E
        dd 0x200000 ; P:0F
        dd 0x60F400 ; P:10
        dd 0x000020 ; P:11
        dd 0x565800 ; P:12
        dd 0x08F484 ; P:13
        dd 0x000001 ; P:14
        dd 0x0AF080 ; P:15
        dd 0x000000 ; P:16
