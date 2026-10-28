/*
 * The data files the core carries (data/), as the assembler's .incbin puts
 * them in: the symbols libretro/gen/data/*.h declare, without the 30 MB of
 * generated C (scripts/pack-file.py) meson compiles them from.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#if defined(__APPLE__)
#define SYM(x) _##x
#define RODATA .const_data
#elif defined(_WIN32)
#define SYM(x) x
#define RODATA .section .rdata,"dr"
#else
#define SYM(x) x
#define RODATA .section .rodata
#endif

/* Each file as NAME_data, NAME_size its length. Written out, not as a
 * macro: ';' does not end a statement in every assembler (Apple's arm64 one
 * reads it as a comment). */
        RODATA

        .globl SYM(segaboot_logo_data)
        .globl SYM(segaboot_logo_size)
        .balign 16
SYM(segaboot_logo_data):
        .incbin "data/segaboot_logo.rgba"
segaboot_logo_end:
        .byte 0
        .balign 4
SYM(segaboot_logo_size):
        .long segaboot_logo_end - SYM(segaboot_logo_data)

        .globl SYM(segaboot_jingle_data)
        .globl SYM(segaboot_jingle_size)
        .balign 16
SYM(segaboot_jingle_data):
        .incbin "data/segaboot_jingle.raw"
segaboot_jingle_end:
        .byte 0
        .balign 4
SYM(segaboot_jingle_size):
        .long segaboot_jingle_end - SYM(segaboot_jingle_data)

        .globl SYM(shader_seeds_data)
        .globl SYM(shader_seeds_size)
        .balign 16
SYM(shader_seeds_data):
        .incbin "data/shader_seeds.bin"
shader_seeds_end:
        .byte 0
        .balign 4
SYM(shader_seeds_size):
        .long shader_seeds_end - SYM(shader_seeds_data)

        .globl SYM(xbox_hdd_data)
        .globl SYM(xbox_hdd_size)
        .balign 16
SYM(xbox_hdd_data):
        .incbin "data/xbox_hdd.qcow2"
xbox_hdd_end:
        .byte 0
        .balign 4
SYM(xbox_hdd_size):
        .long xbox_hdd_end - SYM(xbox_hdd_data)

#if defined(__linux__) && defined(__ELF__)
        .section .note.GNU-stack,"",%progbits
#endif
