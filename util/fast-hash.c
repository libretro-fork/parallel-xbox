#include "qemu/fast-hash.h"
#include <encodings/xxh3.h>

uint64_t fast_hash(const uint8_t *data, size_t len)
{
    return xxh3_64(data, len, 0);
}
