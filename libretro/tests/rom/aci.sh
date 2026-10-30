#!/bin/sh
# Builds aci.inc from aci.c: freestanding 32-bit code linked at ACI_BASE in
# the flash (flash.asm puts it there with -DACI), as bytes, with the entry
# point's address. Needs a gcc that takes -m32 and GNU ld; the result is
# committed, so the tests do not.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
base=0xFFF20000
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mno-sse -mno-mmx -O2 -Wall -c -o "$work/aci.o" "$here/aci.c"
ld -m elf_i386 -Ttext=$base -e aci_main -o "$work/aci.elf" "$work/aci.o"
entry=$(nm "$work/aci.elf" | awk '$3 == "aci_main" { print $1 }')
objcopy -O binary -j .text -j .rodata -j .data "$work/aci.elf" "$work/aci.bin"
{
    echo "; aci.c built by pad.sh: 32-bit code at $base, entered at aci_main;"
    echo "; included at the top of flash.asm, which lays the bytes out with aci_bytes"
    echo "%define ACI_BASE $base"
    echo "%define ACI_ENTRY 0x$entry"
    echo "%macro aci_bytes 0"
    od -An -v -tx1 "$work/aci.bin" | sed 's/^ *//; s/ *$//; s/ /, 0x/g; s/^/        db 0x/'
    echo "%endmacro"
} > "$here/aci.inc"
rm -rf "$work"
echo "aci.inc: $(wc -c < "$here/aci.inc") bytes, entry 0x$entry"
