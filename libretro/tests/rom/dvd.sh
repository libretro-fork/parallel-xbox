#!/bin/sh
# Builds dvd.inc from dvd.c: freestanding 32-bit code linked at DVD_BASE in
# the flash (flash.asm puts it there with -DDVD), as bytes, with the entry
# point's address. Needs a gcc that takes -m32 and GNU ld; the result is
# committed, so the tests do not.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
base=0xFFF18000
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mno-sse -mno-mmx -O2 -Wall -c -o "$work/dvd.o" "$here/dvd.c"
ld -m elf_i386 -Ttext=$base -e dvd_main -o "$work/dvd.elf" "$work/dvd.o"
entry=$(nm "$work/dvd.elf" | awk '$3 == "dvd_main" { print $1 }')
objcopy -O binary -j .text -j .rodata -j .data "$work/dvd.elf" "$work/dvd.bin"
{
    echo "; dvd.c built by dvd.sh: 32-bit code at $base, entered at dvd_main;"
    echo "; included at the top of flash.asm, which lays the bytes out with dvd_bytes"
    echo "%define DVD_BASE $base"
    echo "%define DVD_ENTRY 0x$entry"
    echo "%macro dvd_bytes 0"
    od -An -v -tx1 "$work/dvd.bin" | sed 's/^ *//; s/ *$//; s/ /, 0x/g; s/^/        db 0x/'
    echo "%endmacro"
} > "$here/dvd.inc"
rm -rf "$work"
echo "dvd.inc: $(wc -c < "$here/dvd.inc") bytes, entry 0x$entry"
