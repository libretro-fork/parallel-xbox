# MCPX 1.0 HLE boot

When the retail Xbox has no configured MCPX ROM, the machine runs host-side
boot code after the normal PC/device reset. The frontend selects this path
when the ROM file is absent; an existing file with the wrong size or a read
error is rejected. Chihiro still executes its own BIOS boot code.

The implementation was authored from public behavior descriptions, without
reading or disassembling a console MCPX image. It is an independent behavioral
implementation, not a claim of an audited, two-team clean-room process.

Sources used:

- [XboxDevWiki boot process](https://xboxdevwiki.net/Boot_Process): Xcode
  semantics, RC4 algorithm, scratch RAM and second-stage bootloader layout.
- [XboxDevWiki MCPX ROM](https://xboxdevwiki.net/MCPX_ROM): protected-mode
  startup and the ROM overlay.
- [Fancy Mouse Boot ROM](https://github.com/SnowyMouse/fancy-mouse-boot-rom/blob/b4b62ed6786ea4e39a4dedf94dc340e3df584bc8/mouse.S):
  the published 16-byte `arc4_key_0` interoperability constant only; no
  executable implementation was copied. The open implementation was also
  compiled and run as a black-box boot reference after implementing HLE.
- [Xbox security research, 22C3](https://events.ccc.de/congress/2005/fahrplan/attachments/591-paper_xbox.pdf):
  second-stage boot signature `0x7854794a`.
- [RFC 6229](https://www.rfc-editor.org/rfc/rfc6229): independent RC4 test
  vectors.

The HLE path establishes flat protected-mode segments and a data-only GDT in
the existing writable boot overlay. BIOS Xcodes operate on QEMU's memory and
I/O buses, including PCI configuration accesses. The PCI ROM-disable bit is
masked for Xcodes, as on MCPX. A one-million-instruction limit prevents a bad
BIOS from hanging the host during reset. Exceeding it logs an error and halts
the guest, without terminating the frontend.

The 24 KiB encrypted 2BL is read from `0xffff9e00`, decrypted into `0x90000`,
and entered via its first dword. RC4 scratch data is placed at `0x8f000`.
Signature failure hides the ROM and transfers to address zero, the path used
by free BIOSes that install a failure payload using Xcodes. The HLE path runs
again on reset; snapshot-load resets skip it so the restored CPU and device
state wins. There is no HLE interpreter state to serialize.

This targets MCPX 1.0, not 1.1/FBL boot. It does not reproduce the original
ROM's instruction sequence, timing, GDT addresses, cache/MTRR programming,
or every incidental register value. BIOSes depending on those details may
still need an external ROM. It does not supply the flash BIOS or kernel.

Run the interpreter/RC4 tests without any console firmware:

```
cc -std=c11 -O1 -g -Wall -fsanitize=undefined -I. \
  libretro/tests/test-mcpx-boot.c -o mcpxcheck
./mcpxcheck
```

These check each documented Xcode operation, indirect dispatch, port widths,
PCI masking, backward branches, scratch state, termination and RC4 against
RFC 6229 at offsets 0 and 4096, plus a full 24 KiB round trip. For integration
testing, use a separate system directory with a compatible flash BIOS and no
`mcpx_1.0.bin`; check cold boot, reset and state restore with the headless
libretro harness. Keep the user's normal HDD/EEPROM out of the test directory.

The firmware-free integration test assembles a synthetic encrypted 2BL and
checks the CPU/GDT handoff, Xcode effects, boot-overlay unmapping, a second
boot on reset and RAM-exact snapshot restore at both 256 KiB and 1 MiB flash
sizes. It also checks the signature-failure payload, looping Xcodes and
malformed external-ROM handling:

```
python3 libretro/tests/test-mcpx-hle.py /path/to/core.so \
  /path/to/harness /path/to/new-test-directory
```

It needs NASM and the Linux headless harness, and creates its own disk images.
Synthetic tests establish the boot contract; they do not establish game or
retail-BIOS compatibility by themselves.

Validation for this change on Linux x86-64 with Mesa software OpenGL:

- Full libretro core build; interpreter and RC4 tests with UBSan: pass.
- Firmware-free integration tests above: pass for both flash sizes,
  including reset, byte-for-byte RAM restore and both failure paths.
- Complex 4627 v1.03 (1 MiB): reaches the bundled xemu-dashboard with no
  MCPX image on cold boot. HLE and Fancy Mouse rev0 both reach the same dashboard menu
  at 3600 frames and have the same audio hash. Their clock and animated
  background differ, so their screenshots are not pixel-identical.

The real-BIOS reset/state sequence also eventually renders the dashboard:
8000 frames, reset at 2000, save at 6500, load at 7000, and screenshot at
7999. RAM differs by zero bytes immediately after the load. Shorter runs
captured black screens before dashboard startup completed, with both HLE
and a supplied Fancy Mouse ROM; those captures were not sufficient evidence
of a persistent reset failure. The synthetic test independently checks a
second boot by counting entries into its bootloader.

This is boot validation for that BIOS and dashboard, not an all-games
compatibility claim or a comparison of every register against original ROM.

Native Windows x86-64 validation on 2026-10-30 used the Windows CI artifact
from commit `fb7dfdcc586a7ace1090aa369998eadd4d928bed`, with the native Windows
headless harness (not Wine). An isolated system directory held a copy of
Complex 4627 v1.03 and a fresh disk; no MCPX file was present. Cold boot
reached xemu-dashboard at 3600 frames. An 8000-frame run with reset at 2000,
save at 6500 and load at 7000 also rendered the dashboard at frame 7999.
The load restored RAM with zero differing bytes, and the log confirmed
the experimental HLE path and tested core commit. The installed RetroArch
directory and its disk were not modified.
