; Nested production-size blocks: deferred ADD flags -> SR read, register
; BSET, SR restore and direct program-memory read, then conditional moves.
; Bit 20 is cleared by the architectural SR writable mask; P:(R1) is read
; at runtime into Y0. Both paths stay within active sample/pass DO loops.
        dd 0x0A8581 ; P:00
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x0640A0 ; P:04
        dd 0x028048 ; P:05
        dd 0x060000 ; P:06
        dd 0x00001B ; P:07
        dd 0x61F400 ; P:08
        dd 0x000000 ; P:09
        dd 0x60F400 ; P:0a
        dd 0x001400 ; P:0b
        dd 0x064080 ; P:0c
        dd 0x00001A ; P:0d
        dd 0x44E000 ; P:0e
        dd 0x200048 ; P:0f
        dd 0x0444B9 ; P:10
        dd 0x0AC474 ; P:11
        dd 0x04C4B9 ; P:12
        dd 0x07E186 ; P:13
        dd 0x033203 ; P:14
        dd 0x02B008 ; P:15
        dd 0x0445B9 ; P:16
        dd 0x04D1A1 ; P:17
        dd 0x0452A1 ; P:18
        dd 0x575800 ; P:19
        dd 0x000000 ; P:1a
        dd 0x000000 ; P:1b
        dd 0x08F484 ; P:1c
        dd 0x000001 ; P:1d
        dd 0x0AF080 ; P:1e
        dd 0x000000 ; P:1f
