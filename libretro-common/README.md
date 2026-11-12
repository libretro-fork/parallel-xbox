# libretro-common

The subset used by this core, refreshed from
[RetroArch](https://github.com/libretro/RetroArch/tree/c7a5f6edb8aecfe15d4aad5c3df3c29008f4abdf/libretro-common)
commit `c7a5f6edb8aecfe15d4aad5c3df3c29008f4abdf`.
`UPSTREAM.json` records the source revision and normalized file hashes.
Files retain their upstream licenses.

Existing core adaptations are retained across the refresh:

- `RETRO_DISABLE_HOST_CLOCK`, fixed-spin thread waits and untimed core paths.
- Eventcount initialization that rejects lock/condition-variable fallback.
- SPSC pair publication, contiguous producer reservation and paired borrowed
  consumer views; the upstream queue otherwise matches this copy.
- Paired XXH3 hashing without allocating a concatenated payload.
- The intrusive MPSC header, absent from this RetroArch snapshot.

These adaptations must not be lost by copying the upstream tree wholesale.
Clock-related changes were merged against the original RetroArch import
`af622105435d776c1ce203c28387cbc27e0c7925`, preserving later upstream changes
and the core's clockless contract. `time/rtime.c` remains excluded from the
core build. Existing GL dispatch and macOS JIT permission adapters are kept.

VFS v6 provides optional positional reads. The core negotiates v6 before
accessing appended function pointers, falls back to v3 on older frontends,
and retains seek/read for handles without positional I/O. Local writable
disk images continue to use native positional/vectored I/O.

`retro_deque` was reviewed but is not imported: it is single-owner and does
not replace the core's cross-thread queues. Sequential `data_transfer`
windows likewise do not replace random-access Xbox disk images. No unused
decoder or transfer subsystem is added by this refresh.
