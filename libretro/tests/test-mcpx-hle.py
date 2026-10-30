#!/usr/bin/env python3
"""Firmware-free Linux integration test: CORE HARNESS NEW_WORK_DIRECTORY.

Needs nasm and the existing headless harness (OpenGL/EGL). Never uses the
user's system directory. Tests synthetic 256 KiB and 1 MiB flash images.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: test-mcpx-hle.py CORE HARNESS NEW_WORK_DIRECTORY")

core, harness, work = map(lambda p: Path(p).resolve(), sys.argv[1:])
work.mkdir(parents=True, exist_ok=False)
payload = work / "2bl.bin"
subprocess.run(["nasm", "-f", "bin", "-o", str(payload),
                str(Path(__file__).parent / "rom/mcpx-hle.asm")], check=True)

# Independent test-image encoder, not imported from production code.
key = bytes.fromhex("2745a910397e6aa686fb4b1a4ba90fd2")
s = list(range(256))
j = 0
for i in range(256):
    j = (j + s[i] + key[i % len(key)]) % 256
    s[i], s[j] = s[j], s[i]
encrypted = bytearray(payload.read_bytes())
i = j = 0
for n in range(len(encrypted)):
    i = (i + 1) % 256
    j = (j + s[i]) % 256
    s[i], s[j] = s[j], s[i]
    encrypted[n] ^= s[(s[i] + s[j]) % 256]

env = dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1")
for size in (256 * 1024, 1024 * 1024):
    sysdir = work / str(size)
    xemu = sysdir / "xemu"
    xemu.mkdir(parents=True)
    flash = bytearray(size)
    # Xcodes: RAM marker, attempted ROM-disable PCI write, exit.
    commands = [(3, 0x1004, 0x58434f44), (4, 0x80000880, 2), (0xee, 0, 0)]
    for n, args in enumerate(commands):
        struct.pack_into("<BII", flash, 0x80 + 9 * n, *args)
    flash[-0x6200:-0x200] = encrypted
    struct.pack_into("<I", flash, size - 512 + 8, 0x4f564c59)
    (xemu / "Complex_4627v1.03.bin").write_bytes(flash)
    command = [str(harness), str(core), str(sysdir), "--frames", "12",
               "--reset", "4", "--save", "7", "--load", "9", "--verify-load",
               "--peek", "0x1000", "--peek", "0x1004", "--peek", "0x1008",
               "--peek", "0x100c"]
    result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180)
    (sysdir / "test.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"harness failed: {sysdir / 'test.log'}")
    # The harness prints the guest RAM words in hex at teardown.
    for expected in ("peek_0x1000=0x484c4531", "peek_0x1004=0x58434f44",
                     "peek_0x1008=0x4f564c59", "peek_0x100c=0x00000002"):
        if expected not in result.stdout.lower():
            raise AssertionError(f"missing guest marker {expected}: {sysdir / 'test.log'}")
    if "MCPX ROM absent" not in result.stdout:
        raise AssertionError("HLE fallback was not selected")
    print(f"MCPX HLE {size // 1024} KiB: cold boot/reset/state restore PASS")

# Signature failure must unmap the overlay and reach a BIOS-installed
# address-zero payload, rather than execute the corrupt decrypted 2BL.
sysdir = work / "signature-failure"
xemu = sysdir / "xemu"
xemu.mkdir(parents=True)
flash = bytearray(256 * 1024)
commands = [(3, 0, 0x100005c7), (3, 4, 0x494c0000),
            (3, 8, 0xfeeb4641), (0xee, 0, 0)]
for n, args in enumerate(commands):
    struct.pack_into("<BII", flash, 0x80 + n * 9, *args)
(xemu / "Complex_4627v1.03.bin").write_bytes(flash)
result = subprocess.run([str(harness), str(core), str(sysdir), "--frames", "4",
                         "--peek", "0x1000"], env=env, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
(sysdir / "test.log").write_text(result.stdout)
assert result.returncode == 0, sysdir / "test.log"
assert "peek_0x1000=0x4641494c" in result.stdout.lower(), sysdir / "test.log"
assert "signature mismatch" in result.stdout, sysdir / "test.log"
print("MCPX HLE signature-failure payload: PASS")

# Endless Xcode branch must return control to the host with the CPU halted.
sysdir = work / "xcode-loop"
xemu = sysdir / "xemu"
xemu.mkdir(parents=True)
struct.pack_into("<BII", flash, 0x80, 9, 0, 0xfffffff7)
(xemu / "Complex_4627v1.03.bin").write_bytes(flash)
result = subprocess.run([str(harness), str(core), str(sysdir), "--frames", "4"],
                        env=env, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=180)
(sysdir / "test.log").write_text(result.stdout)
assert result.returncode == 0, sysdir / "test.log"
assert "BIOS Xcodes did not finish" in result.stdout, sysdir / "test.log"
print("MCPX HLE bounded Xcode loop: PASS")

# An existing malformed ROM is an error, not an invitation to select HLE.
(xemu / "mcpx_1.0.bin").write_bytes(b"bad")
result = subprocess.run([str(harness), str(core), str(sysdir), "--frames", "1"],
                        env=env, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=30)
(sysdir / "bad-rom.log").write_text(result.stdout)
assert result.returncode != 0
assert "MCPX Boot ROM must be 512 bytes" in result.stdout
print("Malformed supplied MCPX ROM rejected: PASS")
