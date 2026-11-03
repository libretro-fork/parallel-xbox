#include "block/libretro-vfs-read.h"
#include <assert.h>
#include <stdio.h>

struct retro_vfs_file_handle {
    uint8_t bytes[64];
    size_t position;
    unsigned seeks, reads;
    size_t max_read;
    int seek_error;
    int error;
    unsigned fault_at;
};

static int64_t seek_file(struct retro_vfs_file_handle *f, int64_t off, int where)
{
    assert(where == RETRO_VFS_SEEK_POSITION_START);
    f->seeks++;
    if (f->seek_error) {
        return -1;
    }
    f->position = off;
    return 0; /* libretro VFS contract: success status, not file position */
}

static int64_t read_file(struct retro_vfs_file_handle *f, void *data, uint64_t len)
{
    f->reads++;
    if (f->error && (!f->fault_at || f->reads == f->fault_at)) {
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
    assert(f.reads == reads && cursor == 32);
    f.seek_error = 1;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, adjacent, 3) == -EIO);
    assert(f.reads == reads && cursor == -1);
    f.seek_error = 0;
    seeks = f.seeks;
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, adjacent, 3) == 0);
    assert(f.seeks == seeks + 1 && cursor == 32);
    /* Invalid and empty requests must not contact or reposition a provider. */
    seeks = f.seeks;
    reads = f.reads;
    int64_t saved_cursor = cursor;
    size_t saved_position = f.position;
    memset(out, 0xcc, sizeof(out));
    LRFileIOV empty[] = {{NULL, 0}, {out, 0}};
    assert(lr_vfs_readv(&vfs, &f, &cursor, INT64_MAX, NULL, 0) == 0);
    assert(lr_vfs_readv(&vfs, &f, &cursor, 123, empty, 2) == 0);
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, iov, -1) == -EINVAL);
    assert(lr_vfs_readv(&vfs, &f, &cursor, 0, NULL, 1) == -EINVAL);
    assert(lr_vfs_readv(&vfs, &f, &cursor, -1, NULL, 0) == -EINVAL);
    LRFileIOV overflow[] = {{out, 1}, {out + 24, (size_t)INT64_MAX}};
    if (SIZE_MAX >= INT64_MAX) {
        /* Both an in-file prefix and an EOF prefix used to escape preflight. */
        assert(lr_vfs_readv(&vfs, &f, &cursor, 0, overflow, 2) == -EINVAL);
        assert(lr_vfs_readv(&vfs, &f, &cursor, 64, overflow, 2) == -EINVAL);
    }
    assert(lr_vfs_readv(&vfs, &f, &cursor, INT64_MAX - 16, iov, 2) == -EINVAL);
    assert(f.seeks == seeks && f.reads == reads && cursor == saved_cursor);
    assert(f.position == saved_position);
    for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
    LRFileIOV separated[] = {{NULL,0}, {out,8}, {NULL,0},
                             {out+8,16}, {out+45,0}, {out+24,8}, {NULL,0}};
    LRFileIOV saved[7];
    memcpy(saved,separated,sizeof(saved));
    unsigned cases=0;
    for(unsigned cap=0;cap<=17;cap++)for(unsigned offset=0;offset<=70;offset++){
        f.max_read=cap;f.reads=f.seeks=0;cursor=-1;
        memset(out,0xcc,sizeof(out));
        assert(!lr_vfs_readv(&vfs,&f,&cursor,offset,separated,7));
        size_t available=offset<64?64-offset:0;
        size_t got=available<32?available:32;
        unsigned calls=got?(cap?(unsigned)((got+cap-1)/cap):1):0;
        if(got<32)calls++; /* Confirm EOF exactly once. */
        assert(f.reads==calls && f.seeks==1 && cursor==(int64_t)(offset+got));
        for(unsigned j=0;j<32;j++)assert(out[j]==(j<got?f.bytes[offset+j]:0));
        for(unsigned j=32;j<sizeof(out);j++)assert(out[j]==0xcc);
        assert(!memcmp(saved,separated,sizeof(saved)));
        cases++;
    }
    /* A failed/invalid provider reply after a prefix must not fill untouched
     * bytes or keep a trusted cursor; retry must seek and recover normally. */
    for(unsigned invalid=0;invalid<2;invalid++){
        f.max_read=7;f.reads=f.seeks=0;f.error=invalid?1000:-1;f.fault_at=3;
        cursor=-1;memset(out,0xcc,sizeof(out));
        assert(lr_vfs_readv(&vfs,&f,&cursor,0,separated,7)==-EIO);
        assert(cursor==-1 && f.reads==3 && f.seeks==1);
        assert(!memcmp(out,f.bytes,14));
        for(unsigned j=14;j<sizeof(out);j++)assert(out[j]==0xcc);
        f.error=0;f.fault_at=0;
        assert(!lr_vfs_readv(&vfs,&f,&cursor,0,separated,7));
        assert(f.seeks==2 && cursor==32 && !memcmp(out,f.bytes,32));
        assert(!memcmp(saved,separated,sizeof(saved)));
        cases++;
    }
    printf("VFS empty-separated spans: %u short/EOF/error/recovery cases; contiguous provider reads=1 PASS\n",cases);
    puts("VFS bulk spans, short reads, sequential seeks, gaps, EOF, empty spans and recovery: PASS");
}
