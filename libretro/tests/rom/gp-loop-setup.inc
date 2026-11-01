; Loop setup fixture: frame handshake, DOR #1 with its end on the setup
; extension word, REP #0 (65536 ADDs), then DOR #2 with REP #2 bodies.
; X0=1; each frame adds 65540 to A1 and stores its limited value to X:$20.
; At P:6 the DOR completes during setup. P:8 checks zero-count expansion.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x44F400 ; P:04
        dd 0x000001 ; P:05
        dd 0x060190 ; P:06
        dd 0x000001 ; P:07
        dd 0x0600A0 ; P:08
        dd 0x200040 ; P:09
        dd 0x060290 ; P:0A
        dd 0x000003 ; P:0B
        dd 0x0602A0 ; P:0C
        dd 0x200040 ; P:0D
        dd 0x60F400 ; P:0E
        dd 0x000020 ; P:0F
        dd 0x565800 ; P:10
        dd 0x08F484 ; P:11
        dd 0x000001 ; P:12
        dd 0x0AF080 ; P:13
        dd 0x000000 ; P:14
