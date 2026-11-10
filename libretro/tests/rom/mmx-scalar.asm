; SPDX-License-Identifier: GPL-2.0-or-later
; Compare all scalar MMX forms across signed/lane edges and memory/register aliases.
bits 32
org 0xfff00000
%include "rep-common.inc"
%macro scalar_case 5
%assign mode 0
%rep 3
    inc ebp
    mov dword [0x6000],(%2 & 0xffffffff)
    mov dword [0x6004],((%2 >> 32) & 0xffffffff)
    mov dword [0x6008],(%3 & 0xffffffff)
    mov dword [0x600c],((%3 >> 32) & 0xffffffff)
    movq mm0,[0x6000]
    movq mm1,[0x6008]
    push dword 0x8d7
    popfd
%if mode == 0
    %1 mm0,mm1
%elif mode == 1
    %1 mm0,[0x6008]
%else
    %1 mm0,mm0
%endif
    pushfd
    pop eax
    and eax,0x8d5
    check eax,0x8d5
    movq [0x6100],mm0
%if mode == 2
    check dword [0x6100],(%5 & 0xffffffff)
    check dword [0x6104],((%5 >> 32) & 0xffffffff)
%else
    check dword [0x6100],(%4 & 0xffffffff)
    check dword [0x6104],((%4 >> 32) & 0xffffffff)
%endif
    emms
%assign mode mode+1
%endrep
%endmacro
start:
    rep_test_init
    mov eax,cr0
    and eax,~0x0c
    mov cr0,eax
    mov eax,cr4
    or eax,0x200
    mov cr4,eax
    scalar_case punpcklbw,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case punpcklbw,0x0000000000000000,0xffffffffffffffff,0xff00ff00ff00ff00,0x0000000000000000
    scalar_case punpcklbw,0x0000000000000000,0x8000800080008000,0x8000000080000000,0x0000000000000000
    scalar_case punpcklbw,0x0000000000000000,0x7fff7fff7fff7fff,0x7f00ff007f00ff00,0x0000000000000000
    scalar_case punpcklbw,0x0000000000000000,0x0001000100010001,0x0000010000000100,0x0000000000000000
    scalar_case punpcklbw,0x0000000000000000,0x123456789abcdef0,0x9a00bc00de00f000,0x0000000000000000
    scalar_case punpcklbw,0xffffffffffffffff,0x0000000000000000,0x00ff00ff00ff00ff,0xffffffffffffffff
    scalar_case punpcklbw,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff
    scalar_case punpcklbw,0xffffffffffffffff,0x8000800080008000,0x80ff00ff80ff00ff,0xffffffffffffffff
    scalar_case punpcklbw,0xffffffffffffffff,0x7fff7fff7fff7fff,0x7fffffff7fffffff,0xffffffffffffffff
    scalar_case punpcklbw,0xffffffffffffffff,0x0001000100010001,0x00ff01ff00ff01ff,0xffffffffffffffff
    scalar_case punpcklbw,0xffffffffffffffff,0x123456789abcdef0,0x9affbcffdefff0ff,0xffffffffffffffff
    scalar_case punpcklbw,0x8000800080008000,0x0000000000000000,0x0080000000800000,0x8080000080800000
    scalar_case punpcklbw,0x8000800080008000,0xffffffffffffffff,0xff80ff00ff80ff00,0x8080000080800000
    scalar_case punpcklbw,0x8000800080008000,0x8000800080008000,0x8080000080800000,0x8080000080800000
    scalar_case punpcklbw,0x8000800080008000,0x7fff7fff7fff7fff,0x7f80ff007f80ff00,0x8080000080800000
    scalar_case punpcklbw,0x8000800080008000,0x0001000100010001,0x0080010000800100,0x8080000080800000
    scalar_case punpcklbw,0x8000800080008000,0x123456789abcdef0,0x9a80bc00de80f000,0x8080000080800000
    scalar_case punpcklbw,0x7fff7fff7fff7fff,0x0000000000000000,0x007f00ff007f00ff,0x7f7fffff7f7fffff
    scalar_case punpcklbw,0x7fff7fff7fff7fff,0xffffffffffffffff,0xff7fffffff7fffff,0x7f7fffff7f7fffff
    scalar_case punpcklbw,0x7fff7fff7fff7fff,0x8000800080008000,0x807f00ff807f00ff,0x7f7fffff7f7fffff
    scalar_case punpcklbw,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7f7fffff7f7fffff,0x7f7fffff7f7fffff
    scalar_case punpcklbw,0x7fff7fff7fff7fff,0x0001000100010001,0x007f01ff007f01ff,0x7f7fffff7f7fffff
    scalar_case punpcklbw,0x7fff7fff7fff7fff,0x123456789abcdef0,0x9a7fbcffde7ff0ff,0x7f7fffff7f7fffff
    scalar_case punpcklbw,0x0001000100010001,0x0000000000000000,0x0000000100000001,0x0000010100000101
    scalar_case punpcklbw,0x0001000100010001,0xffffffffffffffff,0xff00ff01ff00ff01,0x0000010100000101
    scalar_case punpcklbw,0x0001000100010001,0x8000800080008000,0x8000000180000001,0x0000010100000101
    scalar_case punpcklbw,0x0001000100010001,0x7fff7fff7fff7fff,0x7f00ff017f00ff01,0x0000010100000101
    scalar_case punpcklbw,0x0001000100010001,0x0001000100010001,0x0000010100000101,0x0000010100000101
    scalar_case punpcklbw,0x0001000100010001,0x123456789abcdef0,0x9a00bc01de00f001,0x0000010100000101
    scalar_case punpcklbw,0x123456789abcdef0,0x0000000000000000,0x009a00bc00de00f0,0x9a9abcbcdedef0f0
    scalar_case punpcklbw,0x123456789abcdef0,0xffffffffffffffff,0xff9affbcffdefff0,0x9a9abcbcdedef0f0
    scalar_case punpcklbw,0x123456789abcdef0,0x8000800080008000,0x809a00bc80de00f0,0x9a9abcbcdedef0f0
    scalar_case punpcklbw,0x123456789abcdef0,0x7fff7fff7fff7fff,0x7f9affbc7fdefff0,0x9a9abcbcdedef0f0
    scalar_case punpcklbw,0x123456789abcdef0,0x0001000100010001,0x009a01bc00de01f0,0x9a9abcbcdedef0f0
    scalar_case punpcklbw,0x123456789abcdef0,0x123456789abcdef0,0x9a9abcbcdedef0f0,0x9a9abcbcdedef0f0
    scalar_case punpcklwd,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case punpcklwd,0x0000000000000000,0xffffffffffffffff,0xffff0000ffff0000,0x0000000000000000
    scalar_case punpcklwd,0x0000000000000000,0x8000800080008000,0x8000000080000000,0x0000000000000000
    scalar_case punpcklwd,0x0000000000000000,0x7fff7fff7fff7fff,0x7fff00007fff0000,0x0000000000000000
    scalar_case punpcklwd,0x0000000000000000,0x0001000100010001,0x0001000000010000,0x0000000000000000
    scalar_case punpcklwd,0x0000000000000000,0x123456789abcdef0,0x9abc0000def00000,0x0000000000000000
    scalar_case punpcklwd,0xffffffffffffffff,0x0000000000000000,0x0000ffff0000ffff,0xffffffffffffffff
    scalar_case punpcklwd,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff
    scalar_case punpcklwd,0xffffffffffffffff,0x8000800080008000,0x8000ffff8000ffff,0xffffffffffffffff
    scalar_case punpcklwd,0xffffffffffffffff,0x7fff7fff7fff7fff,0x7fffffff7fffffff,0xffffffffffffffff
    scalar_case punpcklwd,0xffffffffffffffff,0x0001000100010001,0x0001ffff0001ffff,0xffffffffffffffff
    scalar_case punpcklwd,0xffffffffffffffff,0x123456789abcdef0,0x9abcffffdef0ffff,0xffffffffffffffff
    scalar_case punpcklwd,0x8000800080008000,0x0000000000000000,0x0000800000008000,0x8000800080008000
    scalar_case punpcklwd,0x8000800080008000,0xffffffffffffffff,0xffff8000ffff8000,0x8000800080008000
    scalar_case punpcklwd,0x8000800080008000,0x8000800080008000,0x8000800080008000,0x8000800080008000
    scalar_case punpcklwd,0x8000800080008000,0x7fff7fff7fff7fff,0x7fff80007fff8000,0x8000800080008000
    scalar_case punpcklwd,0x8000800080008000,0x0001000100010001,0x0001800000018000,0x8000800080008000
    scalar_case punpcklwd,0x8000800080008000,0x123456789abcdef0,0x9abc8000def08000,0x8000800080008000
    scalar_case punpcklwd,0x7fff7fff7fff7fff,0x0000000000000000,0x00007fff00007fff,0x7fff7fff7fff7fff
    scalar_case punpcklwd,0x7fff7fff7fff7fff,0xffffffffffffffff,0xffff7fffffff7fff,0x7fff7fff7fff7fff
    scalar_case punpcklwd,0x7fff7fff7fff7fff,0x8000800080008000,0x80007fff80007fff,0x7fff7fff7fff7fff
    scalar_case punpcklwd,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff
    scalar_case punpcklwd,0x7fff7fff7fff7fff,0x0001000100010001,0x00017fff00017fff,0x7fff7fff7fff7fff
    scalar_case punpcklwd,0x7fff7fff7fff7fff,0x123456789abcdef0,0x9abc7fffdef07fff,0x7fff7fff7fff7fff
    scalar_case punpcklwd,0x0001000100010001,0x0000000000000000,0x0000000100000001,0x0001000100010001
    scalar_case punpcklwd,0x0001000100010001,0xffffffffffffffff,0xffff0001ffff0001,0x0001000100010001
    scalar_case punpcklwd,0x0001000100010001,0x8000800080008000,0x8000000180000001,0x0001000100010001
    scalar_case punpcklwd,0x0001000100010001,0x7fff7fff7fff7fff,0x7fff00017fff0001,0x0001000100010001
    scalar_case punpcklwd,0x0001000100010001,0x0001000100010001,0x0001000100010001,0x0001000100010001
    scalar_case punpcklwd,0x0001000100010001,0x123456789abcdef0,0x9abc0001def00001,0x0001000100010001
    scalar_case punpcklwd,0x123456789abcdef0,0x0000000000000000,0x00009abc0000def0,0x9abc9abcdef0def0
    scalar_case punpcklwd,0x123456789abcdef0,0xffffffffffffffff,0xffff9abcffffdef0,0x9abc9abcdef0def0
    scalar_case punpcklwd,0x123456789abcdef0,0x8000800080008000,0x80009abc8000def0,0x9abc9abcdef0def0
    scalar_case punpcklwd,0x123456789abcdef0,0x7fff7fff7fff7fff,0x7fff9abc7fffdef0,0x9abc9abcdef0def0
    scalar_case punpcklwd,0x123456789abcdef0,0x0001000100010001,0x00019abc0001def0,0x9abc9abcdef0def0
    scalar_case punpcklwd,0x123456789abcdef0,0x123456789abcdef0,0x9abc9abcdef0def0,0x9abc9abcdef0def0
    scalar_case punpckldq,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case punpckldq,0x0000000000000000,0xffffffffffffffff,0xffffffff00000000,0x0000000000000000
    scalar_case punpckldq,0x0000000000000000,0x8000800080008000,0x8000800000000000,0x0000000000000000
    scalar_case punpckldq,0x0000000000000000,0x7fff7fff7fff7fff,0x7fff7fff00000000,0x0000000000000000
    scalar_case punpckldq,0x0000000000000000,0x0001000100010001,0x0001000100000000,0x0000000000000000
    scalar_case punpckldq,0x0000000000000000,0x123456789abcdef0,0x9abcdef000000000,0x0000000000000000
    scalar_case punpckldq,0xffffffffffffffff,0x0000000000000000,0x00000000ffffffff,0xffffffffffffffff
    scalar_case punpckldq,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff
    scalar_case punpckldq,0xffffffffffffffff,0x8000800080008000,0x80008000ffffffff,0xffffffffffffffff
    scalar_case punpckldq,0xffffffffffffffff,0x7fff7fff7fff7fff,0x7fff7fffffffffff,0xffffffffffffffff
    scalar_case punpckldq,0xffffffffffffffff,0x0001000100010001,0x00010001ffffffff,0xffffffffffffffff
    scalar_case punpckldq,0xffffffffffffffff,0x123456789abcdef0,0x9abcdef0ffffffff,0xffffffffffffffff
    scalar_case punpckldq,0x8000800080008000,0x0000000000000000,0x0000000080008000,0x8000800080008000
    scalar_case punpckldq,0x8000800080008000,0xffffffffffffffff,0xffffffff80008000,0x8000800080008000
    scalar_case punpckldq,0x8000800080008000,0x8000800080008000,0x8000800080008000,0x8000800080008000
    scalar_case punpckldq,0x8000800080008000,0x7fff7fff7fff7fff,0x7fff7fff80008000,0x8000800080008000
    scalar_case punpckldq,0x8000800080008000,0x0001000100010001,0x0001000180008000,0x8000800080008000
    scalar_case punpckldq,0x8000800080008000,0x123456789abcdef0,0x9abcdef080008000,0x8000800080008000
    scalar_case punpckldq,0x7fff7fff7fff7fff,0x0000000000000000,0x000000007fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckldq,0x7fff7fff7fff7fff,0xffffffffffffffff,0xffffffff7fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckldq,0x7fff7fff7fff7fff,0x8000800080008000,0x800080007fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckldq,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckldq,0x7fff7fff7fff7fff,0x0001000100010001,0x000100017fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckldq,0x7fff7fff7fff7fff,0x123456789abcdef0,0x9abcdef07fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckldq,0x0001000100010001,0x0000000000000000,0x0000000000010001,0x0001000100010001
    scalar_case punpckldq,0x0001000100010001,0xffffffffffffffff,0xffffffff00010001,0x0001000100010001
    scalar_case punpckldq,0x0001000100010001,0x8000800080008000,0x8000800000010001,0x0001000100010001
    scalar_case punpckldq,0x0001000100010001,0x7fff7fff7fff7fff,0x7fff7fff00010001,0x0001000100010001
    scalar_case punpckldq,0x0001000100010001,0x0001000100010001,0x0001000100010001,0x0001000100010001
    scalar_case punpckldq,0x0001000100010001,0x123456789abcdef0,0x9abcdef000010001,0x0001000100010001
    scalar_case punpckldq,0x123456789abcdef0,0x0000000000000000,0x000000009abcdef0,0x9abcdef09abcdef0
    scalar_case punpckldq,0x123456789abcdef0,0xffffffffffffffff,0xffffffff9abcdef0,0x9abcdef09abcdef0
    scalar_case punpckldq,0x123456789abcdef0,0x8000800080008000,0x800080009abcdef0,0x9abcdef09abcdef0
    scalar_case punpckldq,0x123456789abcdef0,0x7fff7fff7fff7fff,0x7fff7fff9abcdef0,0x9abcdef09abcdef0
    scalar_case punpckldq,0x123456789abcdef0,0x0001000100010001,0x000100019abcdef0,0x9abcdef09abcdef0
    scalar_case punpckldq,0x123456789abcdef0,0x123456789abcdef0,0x9abcdef09abcdef0,0x9abcdef09abcdef0
    scalar_case punpckhbw,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case punpckhbw,0x0000000000000000,0xffffffffffffffff,0xff00ff00ff00ff00,0x0000000000000000
    scalar_case punpckhbw,0x0000000000000000,0x8000800080008000,0x8000000080000000,0x0000000000000000
    scalar_case punpckhbw,0x0000000000000000,0x7fff7fff7fff7fff,0x7f00ff007f00ff00,0x0000000000000000
    scalar_case punpckhbw,0x0000000000000000,0x0001000100010001,0x0000010000000100,0x0000000000000000
    scalar_case punpckhbw,0x0000000000000000,0x123456789abcdef0,0x1200340056007800,0x0000000000000000
    scalar_case punpckhbw,0xffffffffffffffff,0x0000000000000000,0x00ff00ff00ff00ff,0xffffffffffffffff
    scalar_case punpckhbw,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff
    scalar_case punpckhbw,0xffffffffffffffff,0x8000800080008000,0x80ff00ff80ff00ff,0xffffffffffffffff
    scalar_case punpckhbw,0xffffffffffffffff,0x7fff7fff7fff7fff,0x7fffffff7fffffff,0xffffffffffffffff
    scalar_case punpckhbw,0xffffffffffffffff,0x0001000100010001,0x00ff01ff00ff01ff,0xffffffffffffffff
    scalar_case punpckhbw,0xffffffffffffffff,0x123456789abcdef0,0x12ff34ff56ff78ff,0xffffffffffffffff
    scalar_case punpckhbw,0x8000800080008000,0x0000000000000000,0x0080000000800000,0x8080000080800000
    scalar_case punpckhbw,0x8000800080008000,0xffffffffffffffff,0xff80ff00ff80ff00,0x8080000080800000
    scalar_case punpckhbw,0x8000800080008000,0x8000800080008000,0x8080000080800000,0x8080000080800000
    scalar_case punpckhbw,0x8000800080008000,0x7fff7fff7fff7fff,0x7f80ff007f80ff00,0x8080000080800000
    scalar_case punpckhbw,0x8000800080008000,0x0001000100010001,0x0080010000800100,0x8080000080800000
    scalar_case punpckhbw,0x8000800080008000,0x123456789abcdef0,0x1280340056807800,0x8080000080800000
    scalar_case punpckhbw,0x7fff7fff7fff7fff,0x0000000000000000,0x007f00ff007f00ff,0x7f7fffff7f7fffff
    scalar_case punpckhbw,0x7fff7fff7fff7fff,0xffffffffffffffff,0xff7fffffff7fffff,0x7f7fffff7f7fffff
    scalar_case punpckhbw,0x7fff7fff7fff7fff,0x8000800080008000,0x807f00ff807f00ff,0x7f7fffff7f7fffff
    scalar_case punpckhbw,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7f7fffff7f7fffff,0x7f7fffff7f7fffff
    scalar_case punpckhbw,0x7fff7fff7fff7fff,0x0001000100010001,0x007f01ff007f01ff,0x7f7fffff7f7fffff
    scalar_case punpckhbw,0x7fff7fff7fff7fff,0x123456789abcdef0,0x127f34ff567f78ff,0x7f7fffff7f7fffff
    scalar_case punpckhbw,0x0001000100010001,0x0000000000000000,0x0000000100000001,0x0000010100000101
    scalar_case punpckhbw,0x0001000100010001,0xffffffffffffffff,0xff00ff01ff00ff01,0x0000010100000101
    scalar_case punpckhbw,0x0001000100010001,0x8000800080008000,0x8000000180000001,0x0000010100000101
    scalar_case punpckhbw,0x0001000100010001,0x7fff7fff7fff7fff,0x7f00ff017f00ff01,0x0000010100000101
    scalar_case punpckhbw,0x0001000100010001,0x0001000100010001,0x0000010100000101,0x0000010100000101
    scalar_case punpckhbw,0x0001000100010001,0x123456789abcdef0,0x1200340156007801,0x0000010100000101
    scalar_case punpckhbw,0x123456789abcdef0,0x0000000000000000,0x0012003400560078,0x1212343456567878
    scalar_case punpckhbw,0x123456789abcdef0,0xffffffffffffffff,0xff12ff34ff56ff78,0x1212343456567878
    scalar_case punpckhbw,0x123456789abcdef0,0x8000800080008000,0x8012003480560078,0x1212343456567878
    scalar_case punpckhbw,0x123456789abcdef0,0x7fff7fff7fff7fff,0x7f12ff347f56ff78,0x1212343456567878
    scalar_case punpckhbw,0x123456789abcdef0,0x0001000100010001,0x0012013400560178,0x1212343456567878
    scalar_case punpckhbw,0x123456789abcdef0,0x123456789abcdef0,0x1212343456567878,0x1212343456567878
    scalar_case punpckhwd,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case punpckhwd,0x0000000000000000,0xffffffffffffffff,0xffff0000ffff0000,0x0000000000000000
    scalar_case punpckhwd,0x0000000000000000,0x8000800080008000,0x8000000080000000,0x0000000000000000
    scalar_case punpckhwd,0x0000000000000000,0x7fff7fff7fff7fff,0x7fff00007fff0000,0x0000000000000000
    scalar_case punpckhwd,0x0000000000000000,0x0001000100010001,0x0001000000010000,0x0000000000000000
    scalar_case punpckhwd,0x0000000000000000,0x123456789abcdef0,0x1234000056780000,0x0000000000000000
    scalar_case punpckhwd,0xffffffffffffffff,0x0000000000000000,0x0000ffff0000ffff,0xffffffffffffffff
    scalar_case punpckhwd,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff
    scalar_case punpckhwd,0xffffffffffffffff,0x8000800080008000,0x8000ffff8000ffff,0xffffffffffffffff
    scalar_case punpckhwd,0xffffffffffffffff,0x7fff7fff7fff7fff,0x7fffffff7fffffff,0xffffffffffffffff
    scalar_case punpckhwd,0xffffffffffffffff,0x0001000100010001,0x0001ffff0001ffff,0xffffffffffffffff
    scalar_case punpckhwd,0xffffffffffffffff,0x123456789abcdef0,0x1234ffff5678ffff,0xffffffffffffffff
    scalar_case punpckhwd,0x8000800080008000,0x0000000000000000,0x0000800000008000,0x8000800080008000
    scalar_case punpckhwd,0x8000800080008000,0xffffffffffffffff,0xffff8000ffff8000,0x8000800080008000
    scalar_case punpckhwd,0x8000800080008000,0x8000800080008000,0x8000800080008000,0x8000800080008000
    scalar_case punpckhwd,0x8000800080008000,0x7fff7fff7fff7fff,0x7fff80007fff8000,0x8000800080008000
    scalar_case punpckhwd,0x8000800080008000,0x0001000100010001,0x0001800000018000,0x8000800080008000
    scalar_case punpckhwd,0x8000800080008000,0x123456789abcdef0,0x1234800056788000,0x8000800080008000
    scalar_case punpckhwd,0x7fff7fff7fff7fff,0x0000000000000000,0x00007fff00007fff,0x7fff7fff7fff7fff
    scalar_case punpckhwd,0x7fff7fff7fff7fff,0xffffffffffffffff,0xffff7fffffff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhwd,0x7fff7fff7fff7fff,0x8000800080008000,0x80007fff80007fff,0x7fff7fff7fff7fff
    scalar_case punpckhwd,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhwd,0x7fff7fff7fff7fff,0x0001000100010001,0x00017fff00017fff,0x7fff7fff7fff7fff
    scalar_case punpckhwd,0x7fff7fff7fff7fff,0x123456789abcdef0,0x12347fff56787fff,0x7fff7fff7fff7fff
    scalar_case punpckhwd,0x0001000100010001,0x0000000000000000,0x0000000100000001,0x0001000100010001
    scalar_case punpckhwd,0x0001000100010001,0xffffffffffffffff,0xffff0001ffff0001,0x0001000100010001
    scalar_case punpckhwd,0x0001000100010001,0x8000800080008000,0x8000000180000001,0x0001000100010001
    scalar_case punpckhwd,0x0001000100010001,0x7fff7fff7fff7fff,0x7fff00017fff0001,0x0001000100010001
    scalar_case punpckhwd,0x0001000100010001,0x0001000100010001,0x0001000100010001,0x0001000100010001
    scalar_case punpckhwd,0x0001000100010001,0x123456789abcdef0,0x1234000156780001,0x0001000100010001
    scalar_case punpckhwd,0x123456789abcdef0,0x0000000000000000,0x0000123400005678,0x1234123456785678
    scalar_case punpckhwd,0x123456789abcdef0,0xffffffffffffffff,0xffff1234ffff5678,0x1234123456785678
    scalar_case punpckhwd,0x123456789abcdef0,0x8000800080008000,0x8000123480005678,0x1234123456785678
    scalar_case punpckhwd,0x123456789abcdef0,0x7fff7fff7fff7fff,0x7fff12347fff5678,0x1234123456785678
    scalar_case punpckhwd,0x123456789abcdef0,0x0001000100010001,0x0001123400015678,0x1234123456785678
    scalar_case punpckhwd,0x123456789abcdef0,0x123456789abcdef0,0x1234123456785678,0x1234123456785678
    scalar_case punpckhdq,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case punpckhdq,0x0000000000000000,0xffffffffffffffff,0xffffffff00000000,0x0000000000000000
    scalar_case punpckhdq,0x0000000000000000,0x8000800080008000,0x8000800000000000,0x0000000000000000
    scalar_case punpckhdq,0x0000000000000000,0x7fff7fff7fff7fff,0x7fff7fff00000000,0x0000000000000000
    scalar_case punpckhdq,0x0000000000000000,0x0001000100010001,0x0001000100000000,0x0000000000000000
    scalar_case punpckhdq,0x0000000000000000,0x123456789abcdef0,0x1234567800000000,0x0000000000000000
    scalar_case punpckhdq,0xffffffffffffffff,0x0000000000000000,0x00000000ffffffff,0xffffffffffffffff
    scalar_case punpckhdq,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff
    scalar_case punpckhdq,0xffffffffffffffff,0x8000800080008000,0x80008000ffffffff,0xffffffffffffffff
    scalar_case punpckhdq,0xffffffffffffffff,0x7fff7fff7fff7fff,0x7fff7fffffffffff,0xffffffffffffffff
    scalar_case punpckhdq,0xffffffffffffffff,0x0001000100010001,0x00010001ffffffff,0xffffffffffffffff
    scalar_case punpckhdq,0xffffffffffffffff,0x123456789abcdef0,0x12345678ffffffff,0xffffffffffffffff
    scalar_case punpckhdq,0x8000800080008000,0x0000000000000000,0x0000000080008000,0x8000800080008000
    scalar_case punpckhdq,0x8000800080008000,0xffffffffffffffff,0xffffffff80008000,0x8000800080008000
    scalar_case punpckhdq,0x8000800080008000,0x8000800080008000,0x8000800080008000,0x8000800080008000
    scalar_case punpckhdq,0x8000800080008000,0x7fff7fff7fff7fff,0x7fff7fff80008000,0x8000800080008000
    scalar_case punpckhdq,0x8000800080008000,0x0001000100010001,0x0001000180008000,0x8000800080008000
    scalar_case punpckhdq,0x8000800080008000,0x123456789abcdef0,0x1234567880008000,0x8000800080008000
    scalar_case punpckhdq,0x7fff7fff7fff7fff,0x0000000000000000,0x000000007fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhdq,0x7fff7fff7fff7fff,0xffffffffffffffff,0xffffffff7fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhdq,0x7fff7fff7fff7fff,0x8000800080008000,0x800080007fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhdq,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhdq,0x7fff7fff7fff7fff,0x0001000100010001,0x000100017fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhdq,0x7fff7fff7fff7fff,0x123456789abcdef0,0x123456787fff7fff,0x7fff7fff7fff7fff
    scalar_case punpckhdq,0x0001000100010001,0x0000000000000000,0x0000000000010001,0x0001000100010001
    scalar_case punpckhdq,0x0001000100010001,0xffffffffffffffff,0xffffffff00010001,0x0001000100010001
    scalar_case punpckhdq,0x0001000100010001,0x8000800080008000,0x8000800000010001,0x0001000100010001
    scalar_case punpckhdq,0x0001000100010001,0x7fff7fff7fff7fff,0x7fff7fff00010001,0x0001000100010001
    scalar_case punpckhdq,0x0001000100010001,0x0001000100010001,0x0001000100010001,0x0001000100010001
    scalar_case punpckhdq,0x0001000100010001,0x123456789abcdef0,0x1234567800010001,0x0001000100010001
    scalar_case punpckhdq,0x123456789abcdef0,0x0000000000000000,0x0000000012345678,0x1234567812345678
    scalar_case punpckhdq,0x123456789abcdef0,0xffffffffffffffff,0xffffffff12345678,0x1234567812345678
    scalar_case punpckhdq,0x123456789abcdef0,0x8000800080008000,0x8000800012345678,0x1234567812345678
    scalar_case punpckhdq,0x123456789abcdef0,0x7fff7fff7fff7fff,0x7fff7fff12345678,0x1234567812345678
    scalar_case punpckhdq,0x123456789abcdef0,0x0001000100010001,0x0001000112345678,0x1234567812345678
    scalar_case punpckhdq,0x123456789abcdef0,0x123456789abcdef0,0x1234567812345678,0x1234567812345678
    scalar_case pmulhw,0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0000000000000000,0xffffffffffffffff,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0000000000000000,0x8000800080008000,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0000000000000000,0x7fff7fff7fff7fff,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0000000000000000,0x0001000100010001,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0000000000000000,0x123456789abcdef0,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0xffffffffffffffff,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0xffffffffffffffff,0xffffffffffffffff,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0xffffffffffffffff,0x8000800080008000,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0xffffffffffffffff,0x7fff7fff7fff7fff,0xffffffffffffffff,0x0000000000000000
    scalar_case pmulhw,0xffffffffffffffff,0x0001000100010001,0xffffffffffffffff,0x0000000000000000
    scalar_case pmulhw,0xffffffffffffffff,0x123456789abcdef0,0xffffffff00000000,0x0000000000000000
    scalar_case pmulhw,0x8000800080008000,0x0000000000000000,0x0000000000000000,0x4000400040004000
    scalar_case pmulhw,0x8000800080008000,0xffffffffffffffff,0x0000000000000000,0x4000400040004000
    scalar_case pmulhw,0x8000800080008000,0x8000800080008000,0x4000400040004000,0x4000400040004000
    scalar_case pmulhw,0x8000800080008000,0x7fff7fff7fff7fff,0xc000c000c000c000,0x4000400040004000
    scalar_case pmulhw,0x8000800080008000,0x0001000100010001,0xffffffffffffffff,0x4000400040004000
    scalar_case pmulhw,0x8000800080008000,0x123456789abcdef0,0xf6e6d4c432a21088,0x4000400040004000
    scalar_case pmulhw,0x7fff7fff7fff7fff,0x0000000000000000,0x0000000000000000,0x3fff3fff3fff3fff
    scalar_case pmulhw,0x7fff7fff7fff7fff,0xffffffffffffffff,0xffffffffffffffff,0x3fff3fff3fff3fff
    scalar_case pmulhw,0x7fff7fff7fff7fff,0x8000800080008000,0xc000c000c000c000,0x3fff3fff3fff3fff
    scalar_case pmulhw,0x7fff7fff7fff7fff,0x7fff7fff7fff7fff,0x3fff3fff3fff3fff,0x3fff3fff3fff3fff
    scalar_case pmulhw,0x7fff7fff7fff7fff,0x0001000100010001,0x0000000000000000,0x3fff3fff3fff3fff
    scalar_case pmulhw,0x7fff7fff7fff7fff,0x123456789abcdef0,0x09192b3bcd5eef78,0x3fff3fff3fff3fff
    scalar_case pmulhw,0x0001000100010001,0x0000000000000000,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0001000100010001,0xffffffffffffffff,0xffffffffffffffff,0x0000000000000000
    scalar_case pmulhw,0x0001000100010001,0x8000800080008000,0xffffffffffffffff,0x0000000000000000
    scalar_case pmulhw,0x0001000100010001,0x7fff7fff7fff7fff,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0001000100010001,0x0001000100010001,0x0000000000000000,0x0000000000000000
    scalar_case pmulhw,0x0001000100010001,0x123456789abcdef0,0x00000000ffffffff,0x0000000000000000
    scalar_case pmulhw,0x123456789abcdef0,0x0000000000000000,0x0000000000000000,0x014b1d34280e0445
    scalar_case pmulhw,0x123456789abcdef0,0xffffffffffffffff,0xffffffff00000000,0x014b1d34280e0445
    scalar_case pmulhw,0x123456789abcdef0,0x8000800080008000,0xf6e6d4c432a21088,0x014b1d34280e0445
    scalar_case pmulhw,0x123456789abcdef0,0x7fff7fff7fff7fff,0x09192b3bcd5eef78,0x014b1d34280e0445
    scalar_case pmulhw,0x123456789abcdef0,0x0001000100010001,0x00000000ffffffff,0x014b1d34280e0445
    scalar_case pmulhw,0x123456789abcdef0,0x123456789abcdef0,0x014b1d34280e0445,0x014b1d34280e0445
    check ebp,757
    mov dword [R],0x4b4f534d ; MSOK
.halt:
    cli
    hlt
    jmp .halt
    rep_test_handlers
times 0x100000-($-$$) db 0xff
