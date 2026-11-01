; GP frame handshake and nested loops from gp.inc.
; P:13 ADD X0,A produces lazy flags consumed by Jcc EQ absolute EA at P:14.
; P:17 JScc CS immediate and P:18 JScc CC (R1) call P:$30, which JMPs
; to RTS at P:$31. P:24 JMP absolute EA returns to the frame handshake.
; Sample/pass loop ends are P:$14/$15. Output is stored through R0.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x61F400 ; P:04
        dd 0x000030 ; P:05
        dd 0x060000 ; P:06
        dd 0x000015 ; P:07
        dd 0x60F400 ; P:08
        dd 0x001400 ; P:09
        dd 0x064080 ; P:0A
        dd 0x000014 ; P:0B
        dd 0x44E000 ; P:0C
        dd 0x200040 ; P:0D
        dd 0x0AF0AA ; P:0E
        dd 0x000011 ; P:0F
        dd 0x000000 ; P:10
        dd 0x0F8030 ; P:11
        dd 0x0BE1A0 ; P:12
        dd 0x565800 ; P:13
        dd 0x000000 ; P:14
        dd 0x000000 ; P:15
        dd 0x08F484 ; P:16
        dd 0x000001 ; P:17
        dd 0x0AF080 ; P:18
        dd 0x000000 ; P:19
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
        dd 0x0C0031 ; P:30
        dd 0x00000C ; P:31
