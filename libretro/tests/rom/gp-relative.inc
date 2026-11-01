; Relative-control fixture using the GP frame handshake and nested loops.
; P:04 BRCLR X:$FFC5,#1 skips to DO; P:13 ADD X0,A supplies Bcc flags.
; P:14 Bcc EQ short and P:15 BRCLR X0,#0 converge at P:17.
; P:17/P:18 call P:$30 via short/long BSR. P:$30 BRA short reaches RTS.
; P:24 BRA long returns to the frame handshake. Sample/pass ends P:$14/$15.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x0CC501 ; P:04
        dd 0x000002 ; P:05
        dd 0x060000 ; P:06
        dd 0x000015 ; P:07
        dd 0x60F400 ; P:08
        dd 0x001400 ; P:09
        dd 0x064080 ; P:0A
        dd 0x000014 ; P:0B
        dd 0x44E000 ; P:0C
        dd 0x200040 ; P:0D
        dd 0x05A403 ; P:0E
        dd 0x0CC480 ; P:0F
        dd 0x000002 ; P:10
        dd 0x05081F ; P:11
        dd 0x0D1080 ; P:12
        dd 0x00001E ; P:13
        dd 0x565800 ; P:14
        dd 0x000000 ; P:15
        dd 0x08F484 ; P:16
        dd 0x000001 ; P:17
        dd 0x0D10C0 ; P:18
        dd 0xFFFFE8 ; P:19
        dd 0x000000 ; P:1A
        dd 0x000000 ; P:1B
        dd 0x000000 ; P:1C
        dd 0x000000 ; P:1D
        dd 0x000000 ; P:1E
        dd 0x000000 ; P:1F
        dd 0x000000 ; P:20
        dd 0x000000 ; P:21
        dd 0x000000 ; P:22
        dd 0x000000 ; P:23
        dd 0x000000 ; P:24
        dd 0x000000 ; P:25
        dd 0x000000 ; P:26
        dd 0x000000 ; P:27
        dd 0x000000 ; P:28
        dd 0x000000 ; P:29
        dd 0x000000 ; P:2A
        dd 0x000000 ; P:2B
        dd 0x000000 ; P:2C
        dd 0x000000 ; P:2D
        dd 0x000000 ; P:2E
        dd 0x000000 ; P:2F
        dd 0x050C01 ; P:30
        dd 0x00000C ; P:31
