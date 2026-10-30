#include "block/libretro-vfs-read.h"
#include <assert.h>
#include <stdio.h>

struct retro_vfs_file_handle {
    uint8_t bytes[64];
    size_t position;
    unsigned seeks, reads;
    size_t max_read;
    int error;
};

static int64_t seek_file(struct retro_vfs_file_handle *f, int64_t off, int where)
{
    assert(where == RETRO_VFS_SEEK_POSITION_START);
    f->seeks++;
    f->position = off;
    return off;
}

static int64_t read_file(struct retro_vfs_file_handle *f, void *data, uint64_t len)
{
    f->reads++;
    if (f->error) {
        return f->error;
    }
    if (f->position >= sizeof(f->bytes)) {
        return 0;
    }
    if (f->max_read && len > f->max_read) { len = f->max_read; }
    if (len > sizeof(f->bytes) - f->position) {
        len = sizeof(f->bytes) - f->position;
    }
    memcpy(data, f->bytes + f->position, len);
    f->position += len;
    return len;
}

int main(void)
{
    struct retro_vfs_interface vfs = { .seek = seek_file, .read = read_file };
    struct retro_vfs_file_handle f = { .max_read = 7 };
    for (unsigned i = 0; i < sizeof(f.bytes); i++) {
        f.bytes[i] = i;
    }
    uint8_t out[48];
    LRFileIOV iov[] = { {out, 16}, {out + 24, 16} };
    int64_t cursor = 0;
    memset(out, 0xcc, sizeof(out));
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, iov, 2) == 0);
    assert(cursor == 32 && f.seeks == 0 && f.reads == 6);
    assert(!memcmp(out, f.bytes, 16) && !memcmp(out + 24, f.bytes + 16, 16));
    assert(out[16] == 0xcc && out[47] == 0xcc);
    assert(lr_vfs_readv(&vfs, &f, &cursor, 32, iov, 2) == 0);
    assert(cursor == 64 && f.seeks == 0);
    assert(lr_vfs_readv(&vfs, &f, &cursor, 60, iov, 2) == 0);
    assert(cursor == 64 && f.seeks == 1);
    assert(!memcmp(out, f.bytes + 60, 4));
    assert(!memcmp(out + 4, (uint8_t[12]) {0}, 12));
    assert(!memcmp(out + 24, (uint8_t[16]) {0}, 16));
    f.error = -1;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, iov, 2) == -EIO && cursor == -1);
    f.error = 0;
    unsigned seeks = f.seeks;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, iov, 2) == 0);
    assert(f.seeks == seeks + 1 && cursor == 32);
    f.error = 1000;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 32, iov, 2) == -EIO);
    f.error = 0;
    f.max_read = 0;
    LRFileIOV adjacent[] = { {out, 8}, {out + 8, 16}, {out + 24, 8} };
    unsigned reads = f.reads;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, adjacent, 3) == 0);
    assert(f.reads == reads + 1 && cursor == 32);
    assert(!memcmp(out, f.bytes, 32));
    /* A real gap still requires two direct reads and must stay untouched. */
    reads = f.reads;
    memset(out, 0xcc, sizeof(out));
    assert(lr_vfs_readv(&vfs, &f, &cursor, 32, iov, 2) == 0);
    assert(f.reads == reads + 2 && cursor == 64);
    assert(!memcmp(out, f.bytes + 32, 16));
    assert(!memcmp(out + 24, f.bytes + 48, 16));
    assert(out[16] == 0xcc && out[47] == 0xcc);
    /* EOF inside a merged span zeroes its remainder and later gapped spans. */
    LRFileIOV eof[] = { {out, 8}, {out + 8, 8}, {out + 24, 8} };
    memset(out, 0xcc, sizeof(out));
    assert(lr_vfs_readv(&vfs, &f, &cursor, 60, eof, 3) == 0);
    assert(!memcmp(out, f.bytes + 60, 4));
    assert(!memcmp(out + 4, (uint8_t[12]) {0}, 12));
    assert(!memcmp(out + 24, (uint8_t[8]) {0}, 8));
    assert(out[16] == 0xcc && out[32] == 0xcc);
    /* Provider short reads can cross the original span boundaries. */
    f.max_read = 7;
    reads = f.reads;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, adjacent, 3) == 0);
    assert(f.reads == reads + 5 && !memcmp(out, f.bytes, 32));
    reads = f.reads;
    assert(lr_vfs_readv(&vfs, &f, &cursor, INT64_MAX - 3, adjacent, 3) == -EINVAL);
    assert(f.reads == reads && cursor == INT64_MAX - 3);
    puts("VFS bulk spans, short reads, sequential seeks, gaps, EOF and recovery: PASS");
}
