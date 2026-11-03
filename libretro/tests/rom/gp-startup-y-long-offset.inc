; Long-offset Y-memory control moves and repeated raw SSH stores.
        dd 0x0A8581 ; P:00 frame handshake
        dd 0x000000 ; P:01
        dd 0x08F485 ; P:02
        dd 0x000002 ; P:03
        dd 0x60F400 ; P:04 MOVE #$80,R0
        dd 0x000080 ; P:05
        dd 0x0B70BE ; P:06 MOVE LA,Y:(R0-1)
        dd 0xFFFFFF ; P:07
        dd 0x0B70FE ; P:08 MOVE Y:(R0-1),LA
        dd 0xFFFFFF ; P:09
        dd 0x0602A0 ; P:0A REP #2
        dd 0x0B70BC ; P:0B MOVE SSH,Y:(R0+1); no pop
        dd 0x000001 ; P:0C
        dd 0x0B70BD ; P:0D MOVE SSL,Y:(R0+2)
        dd 0x000002 ; P:0E
        dd 0x0B70FD ; P:0F MOVE Y:(R0+2),SSL
        dd 0x000002 ; P:10
        dd 0x0B70B9 ; P:11 MOVE SR,Y:(R0+3)
        dd 0x000003 ; P:12
        dd 0x0B70F9 ; P:13 MOVE Y:(R0+3),SR
        dd 0x000003 ; P:14
        dd 0x08F484 ; P:15 halt until next frame
        dd 0x000001 ; P:16
        dd 0x0AF080 ; P:17 restart
        dd 0x000000 ; P:18
