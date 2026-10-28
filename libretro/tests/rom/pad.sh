#!/bin/sh
# Builds pad.inc from pad.c: freestanding 32-bit code linked at PAD_BASE in
# the flash (flash.asm puts it there with -DPAD), as bytes, with the entry
# point's address. Needs a gcc that takes -m32 and GNU ld; the result is
# committed, so the tests do not.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
base=0xFFF08000
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mno-sse -mno-mmx -O2 -Wall -c -o "$work/pad.o" "$here/pad.c"
ld -m elf_i386 -Ttext=$base -e pad_main -o "$work/pad.elf" "$work/pad.o"
entry=$(nm "$work/pad.elf" | awk '$3 == "pad_main" { print $1 }')
objcopy -O binary -j .text -j .rodata -j .data "$work/pad.elf" "$work/pad.bin"
{
    echo "; pad.c built by pad.sh: 32-bit code at $base, entered at pad_main;"
    echo "; included at the top of flash.asm, which lays the bytes out with pad_bytes"
    echo "%define PAD_BASE $base"
    echo "%define PAD_ENTRY 0x$entry"
    echo "%macro pad_bytes 0"
    od -An -v -tx1 "$work/pad.bin" | sed 's/^ *//; s/ *$//; s/ /, 0x/g; s/^/        db 0x/'
    echo "%endmacro"
} > "$here/pad.inc"
rm -rf "$work"
echo "pad.inc: $(wc -c < "$here/pad.inc") bytes, entry 0x$entry"
