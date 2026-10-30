# XDVD PR #1659 audit and Otogi fallback

Audited upstream [PR #1659](https://github.com/xemu-project/xemu/pull/1659)
at head `5962570fcd1aded1806397e2c4885af254c2a428`, including its review
discussion. Findings apply to that revision.

## Applicability

The PR covers the missing MODE SENSE/SELECT security page and READ DVD
STRUCTURE challenge table involved in Otogi's startup failure. Native
authentication needs the disc's separately dumped `SS.bin` (2048 bytes) or
`dvd_layout.bin` (1636 bytes). An ordinary ISO contains data sectors, not
the lead-out security structure. OGX_CHALLENGE_TABLE dumps it from a real
disc/drive; RebuildSS repairs an existing SS rather than deriving its signed
data from a game ISO. Inventing a table or accepting arbitrary challenges
does not implement authentication.

References: [dump tool](https://github.com/Ryzee119/OGX_CHALLENGE_TABLE),
[metadata tools](https://github.com/Deterous/ParseXboxMetadata),
[disc layout](https://xboxdevwiki.net/Xbox_Game_Disc),
[drive page](https://xboxdevwiki.net/DVD_Drive), and
[protocol research](https://multimedia.cx/eggs/xbox-sphinx-protocol/).

## Correctness findings

- **High: unbounded challenge count.** The untrusted count at byte 773 drives
  indexing into a 253-byte table without checking the maximum of 23 records.
  A malformed dump can read beyond the decrypted buffer.
- **High: premature authentication and ID-only matching.** MODE SELECT
  forces Authenticated=1 after the first copied page, regardless of challenge
  value or validity. MODE SENSE matches only the ID and returns zero for a
  missing entry, conflating failure with a valid zero response. The PR
  acknowledges that it does not implement the full unlocking sequence.
- **High: incomplete data-out transport.** MODE SELECT always starts DMA,
  even when the guest chose PIO. It does not enforce a complete 28-byte
  parameter list, finish zero-length requests, or correctly complete a short
  PRD transfer. It can copy stale bytes and leave commands incomplete.
- **High: full-image read bounds.** `cmd_read` adds the physical partition
  offset to LBA, then compares it against the smaller logical game-partition
  sector count, rejecting valid reads near the end. Check logical bounds
  first and translate once.
- **Medium: unchecked dump reads.** `fseek`, `ftell`, and `fread` results
  are not checked; file length alone does not validate the format. The
  converted structure's length field is 0x664 instead of the documented
  0x662, which excludes the field's own two bytes.
- **Medium: lifecycle and snapshots.** A global security path does not bind
  data to a mounted disc. Authentication and tables are absent from VMState.
  Pending PIO/DMA transfers need receive-state and restart handling too.
- **Medium: portability and detection.** Packed native uint32_t fields
  assume host byte order/alignment. A fixed image-size heuristic can
  misidentify a same-sized XISO. A configured security path also changes
  media classification independently of successfully loading its data.
- **Medium: reply initialization.** Disabling common reply-buffer clearing
  leaves reserved bytes stale in other structures. Preserve the CDB in
  local fields before clearing response data.
- **Core integration:** this fork uses a static Makefile and frontend
  settings, not meson or xemu.toml. Its block layer already normalizes full
  images; adding the PR's translation would double-offset reads. Debug
  printf output and per-command file opens should not be carried over.
  Shared RC4/SHA1 primitives already exist locally.

## Delivered runtime fallback

The delivered change addresses the requested unmodified-image Otogi case
without claiming a complete native-authentication backport. A bounded
XDVDFS probe reads the root-directory search path and relevant XBE headers.
It selects only Otogi title `46530002`, computes its retail/debug entry,
and arranges an execution-time helper there. A content change flushes code
when the watched entry changes, so cached code cannot omit the helper.
At execution it revalidates the loaded header, title, entry, certificate
and RAM bounds and clears only runtime-security flag bit 0. The kernel can
already have decoded the entry field; the loaded and on-disc forms are
handled separately.

Instruction and disc bytes are not rewritten. The image stays read-only.
The helper repeats on subsequent boots instead of depending on a one-time
host flag. Snapshots after entry retain the adjusted RAM; snapshots before
entry still execute the helper. The option defaults on because otherwise
Otogi returns to the dashboard with the supplied Complex BIOS. Disabling
it retains the original checks. Other title IDs are not modified.

An experimental native-protocol implementation remains off the delivered
branch: without matching signed data and a hardware reference it would not
be correct to present it as validated retail authentication. The native
DVD-security requirement is independent of MCPX HLE.

## File I/O

Read-only local files use libretro-common's demand-paged mapped views when
available. Opening a mapping does not read an image into a heap buffer.
Metadata probes borrow immutable ranges. Scatter reads make the required
bulk copy directly into guest buffers, with no staging buffer or read
syscall on a successful mapping, and combine adjacent spans. This is
zero-copy metadata access, not zero-copy delivery into writable guest RAM.
Writable HDDs and mapping failures retain native positional/vectored I/O,
sparse allocation, and hole punching. Frontend VFS handles avoid repeated
seeks for sequential requests, combine adjacent destination spans without a
staging buffer, and retry partial reads.

The file-I/O test covers sparse offsets beyond 4 GiB, scatter buffers with
gaps, zero-filled EOF reads, view bounds, and rejection of mapped writes
and truncation. Its optional benchmark samples at most 16 MiB of an
existing file, without reading the whole image.
