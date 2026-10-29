#!/bin/sh
# Builds gpu.inc from gpu.c: freestanding 32-bit code linked at GPU_BASE in
# the flash (flash.asm puts it there with -DGPU), as bytes, with the entry
# point's address. Needs a gcc that takes -m32 and GNU ld; the result is
# committed, so the tests do not.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
base=0xFFF10000
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mno-sse -mno-mmx -O2 -Wall -c -o "$work/gpu.o" "$here/gpu.c"
ld -m elf_i386 -Ttext=$base -e gpu_main -o "$work/gpu.elf" "$work/gpu.o"
entry=$(nm "$work/gpu.elf" | awk '$3 == "gpu_main" { print $1 }')
objcopy -O binary -j .text -j .rodata -j .data "$work/gpu.elf" "$work/gpu.bin"
{
    echo "; gpu.c built by pad.sh: 32-bit code at $base, entered at gpu_main;"
    echo "; included at the top of flash.asm, which lays the bytes out with gpu_bytes"
    echo "%define GPU_BASE $base"
    echo "%define GPU_ENTRY 0x$entry"
    echo "%macro gpu_bytes 0"
    od -An -v -tx1 "$work/gpu.bin" | sed 's/^ *//; s/ *$//; s/ /, 0x/g; s/^/        db 0x/'
    echo "%endmacro"
} > "$here/gpu.inc"
rm -rf "$work"
echo "gpu.inc: $(wc -c < "$here/gpu.inc") bytes, entry 0x$entry"
