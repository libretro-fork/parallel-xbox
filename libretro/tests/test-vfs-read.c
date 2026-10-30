#include "block/libretro-vfs-read.h"
#include <assert.h>
#include <stdio.h>

struct retro_vfs_file_handle {
    uint8_t bytes[64];
    size_t position;
    unsigned seeks, reads;
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
    if (len > 7) { len = 7; } /* legitimate short successful reads */
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
    struct retro_vfs_file_handle f = {0};
    for (unsigned i = 0; i < sizeof(f.bytes); i++) {
        f.bytes[i] = i;
    }
    uint8_t out[48];
    LRFileIOV iov[] = { {out, 16}, {out + 24, 16}, {NULL, 0} };
    int64_t cursor = 0;
    memset(out, 0xcc, sizeof(out));
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, iov, 2) == 0);
    assert(cursor == 32 && f.seeks == 0 && f.reads == 6);
    assert(!memcmp(out, f.bytes, 16) && !memcmp(out + 24, f.bytes + 16, 16));
    assert(out[16] == 0xcc && out[47] == 0xcc);
    assert(lr_vfs_readv(&vfs, &f, &cursor, 32, iov, 2) == 0);
    assert(cursor == 64 && f.seeks == 0);
    /* EOF zero-fill must ignore a null, empty trailing vector. */
    assert(lr_vfs_readv(&vfs, &f, &cursor, 60, iov, 3) == 0);
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
    puts("VFS short reads, sequential seeks, scatter gaps, EOF, empty spans and recovery: PASS");
}
