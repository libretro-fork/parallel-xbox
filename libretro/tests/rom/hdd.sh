#!/bin/sh
# Builds hdd.inc from hdd.c: freestanding 32-bit code linked at HDD_BASE in
# the flash (flash.asm puts it there with -DHDD), as bytes, with the entry
# point's address. Needs a gcc that takes -m32 and GNU ld; the result is
# committed, so the tests do not.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
base=0xFFF1C000
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mno-sse -mno-mmx -O2 -Wall -c -o "$work/hdd.o" "$here/hdd.c"
ld -m elf_i386 -Ttext=$base -e hdd_main -o "$work/hdd.elf" "$work/hdd.o"
entry=$(nm "$work/hdd.elf" | awk '$3 == "hdd_main" { print $1 }')
objcopy -O binary -j .text -j .rodata -j .data "$work/hdd.elf" "$work/hdd.bin"
{
    echo "; hdd.c built by hdd.sh: 32-bit code at $base, entered at hdd_main;"
    echo "; included at the top of flash.asm, which lays the bytes out with hdd_bytes"
    echo "%define HDD_BASE $base"
    echo "%define HDD_ENTRY 0x$entry"
    echo "%macro hdd_bytes 0"
    od -An -v -tx1 "$work/hdd.bin" | sed 's/^ *//; s/ *$//; s/ /, 0x/g; s/^/        db 0x/'
    echo "%endmacro"
} > "$here/hdd.inc"
rm -rf "$work"
echo "hdd.inc: $(wc -c < "$here/hdd.inc") bytes, entry 0x$entry"
