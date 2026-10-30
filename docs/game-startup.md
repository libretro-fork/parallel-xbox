# Xbox game startup checks

The following comparisons used the native Windows core at `860e7cae0`,
Complex 4627 v1.03, and isolated copies of the same EEPROM and fresh disk.
Both the supplied original MCPX 1.0 ROM and the HLE fallback were tested.

## Dead or Alive 2 Ultimate

Allow more than a minute before treating its initial black screen as a
failure. In both boot modes it was still black at frame 5399 (about 90
seconds), and displayed the legal screen by frame 5999 (about 100 seconds).
Both completed a 36000-frame startup/intro run without a process failure or
audio-cadence error. These are emulated times; wall time depends on speed.
This establishes startup, rather than complete gameplay compatibility.

## Otogi: Myth of Demons

Runtime Disc-check Compatibility is enabled by default under System. With
Complex 4627, load the original XISO or full disc image directly. At its entry
point the core clears bit 0 of the loaded Otogi certificate's runtime-security
flags; it does not alter the ISO, executable sections, BIOS, or on-disk saves.
The loaded title ID, decoded entry point, certificate bounds, and RAM mappings
are checked before the four-byte write. The same action runs on later boots
and works with cached translated code. Disable the option and reload content
to retain the original checks. This is a compatibility fallback, not physical
DVD authentication or support for booting retail BIOSes without their security
data. See the [PR audit](xdvd-pr1659-audit.md).

An unadjusted retail image can return to the dashboard immediately after
the Xbox logo. This was reproduced in both boot modes and in standalone
xemu 0.8.136 (the core's upstream base, without the fork's later changes).
The game requests MODE SENSE page `0x3e`, which the emulated drive rejects;
the title's runtime disc-check flag also requests the physical disc's
security sector. Repacking the filesystem alone did not fix startup.

As an optional offline alternative for Complex 4627, clearing bit 0 of `default.xbe`'s runtime security flags
in a separate image lets startup continue. The supplied utility makes a
new image and changes only this flag when explicitly requested:

```
python libretro/tools/prepare-xiso.py "Otogi.iso" "Otogi-xemu.iso" --disable-runtime-disc-check
```

Load the new image in RetroArch. Existing output files are rejected. The
input is read-only; executable sections and all game assets are preserved.
The tool accepts an XISO at offset zero or a full image with its Xbox
partition at offset 405798912. It copies the partition without compacting
the filesystem. The flag adjustment requires a BIOS that accepts modified
headers, such as the tested Complex BIOS; it does not implement physical
disc authentication in the emulator. Only the root `default.xbe` is adjusted.

The header-adjusted image reached gameplay in both boot modes with the
unmodified Windows core (9000 frames each, title `46530002`, zero process
failures and zero audio-cadence errors). This is an image-preparation
workaround, not an MCPX HLE change. Full-game stability is not established.

The utility's firmware-free checks cover partition extraction, preservation
of input and unrelated flag bits, existing-output rejection, and malformed
certificate rejection:

```
python libretro/tests/test-prepare-xiso.py
```

Format references: [XBE certificate](https://xboxdevwiki.net/Xbe) and
[Xbox DVD security page](https://xboxdevwiki.net/DVD_Drive).
