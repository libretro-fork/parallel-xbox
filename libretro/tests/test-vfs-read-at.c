/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "block/libretro-vfs-read.h"
#include "block/libretro-vfs-negotiate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct retro_vfs_file_handle {
    uint8_t bytes[64];
    int64_t position;
    unsigned seeks, reads, at_reads, limit, decline_at, invalid_at;
};

static int64_t seek_file(struct retro_vfs_file_handle *f, int64_t off, int whence)
{
    assert(whence == RETRO_VFS_SEEK_POSITION_START);
    f->seeks++;
    f->position = off;
    return 0;
}

static int64_t copy_file(struct retro_vfs_file_handle *f, void *dst,
                         uint64_t n, int64_t off)
{
    assert(off >= 0);
    if (off >= 64) return 0;
    if (n > (uint64_t)(64-off)) n = 64-off;
    if (f->limit && n > f->limit) n = f->limit;
    memcpy(dst, f->bytes+off, n);
    return n;
}

static int64_t read_file(struct retro_vfs_file_handle *f, void *dst, uint64_t n)
{
    f->reads++;
    int64_t got = copy_file(f, dst, n, f->position);
    f->position += got;
    return got;
}

static int64_t read_at(struct retro_vfs_file_handle *f, void *dst,
                       uint64_t n, int64_t off)
{
    f->at_reads++;
    if (f->at_reads == f->decline_at) return -1;
    if (f->at_reads == f->invalid_at) return (int64_t)n+1;
    return copy_file(f, dst, n, off);
}

static struct retro_vfs_interface vfs = {
    .seek=seek_file, .read=read_file, .read_at=read_at
};
static unsigned offered, requests[2], requested;
static bool environment(unsigned cmd, void *data)
{
    struct retro_vfs_interface_info *info=data;
    assert(cmd == RETRO_ENVIRONMENT_GET_VFS_INTERFACE && requested < 2);
    requests[requested++]=info->required_interface_version;
    if (info->required_interface_version > offered) {
        info->iface = &vfs; /* A failed frontend may leave stale output. */
        return false;
    }
    info->iface = &vfs;
    return true;
}

int main(void)
{
    unsigned cases=0;
    for (offered=0;offered<=6;offered++) {
        struct retro_vfs_interface_info info;
        requested=0;
        assert(lr_vfs_negotiate(environment,&info)==(offered>=3));
        assert(requests[0]==6);
        if(offered<6)assert(requested==2 && requests[1]==3);
        assert(lr_vfs_positional_read(&info)==(offered>=6?read_at:NULL));
    }
    /* An older table ends before the v6 members. Do not read its tail. */
    union { max_align_t align;
        uint8_t prefix[offsetof(struct retro_vfs_interface,stat_64)]; } legacy={0};
    struct retro_vfs_interface_info old={3,(void *)legacy.prefix};
    assert(old.iface && !lr_vfs_positional_read(&old));
    for(unsigned cap=0;cap<=17;cap++)for(unsigned off=0;off<=70;off++)
    for(unsigned decline=0;decline<=4;decline++) {
        struct retro_vfs_file_handle f={.position=57,.limit=cap,.decline_at=decline};
        for(unsigned i=0;i<64;i++)f.bytes[i]=(uint8_t)(i+1);
        uint8_t dst[48];memset(dst,0xcc,sizeof(dst));
        LRFileIOV iov[]={{dst,8},{NULL,0},{dst+8,8},{dst+24,16},{NULL,0}};
        int64_t cursor=f.position;
        assert(!lr_vfs_readv_at(&vfs,&f,read_at,&cursor,off,iov,5));
        for(unsigned j=0;j<32;j++) {
            unsigned where=j<16?j:j+8;
            assert(dst[where]==(off+j<64?f.bytes[off+j]:0));
        }
        for(unsigned j=16;j<24;j++)assert(dst[j]==0xcc);
        for(unsigned j=40;j<48;j++)assert(dst[j]==0xcc);
        if(!decline || f.at_reads<decline) {
            assert(!f.seeks && !f.reads && cursor==57 && f.position==57);
        } else {
            assert(f.seeks<=1 && f.reads && cursor==f.position);
        }
        cases++;
    }
    struct retro_vfs_file_handle f={.invalid_at=1};
    uint8_t out[32];memset(out,0xcc,sizeof(out));
    LRFileIOV iov={out,sizeof(out)};int64_t cursor=0;
    assert(lr_vfs_readv_at(&vfs,&f,read_at,&cursor,0,&iov,1)==-EIO);
    assert(!f.reads && !f.seeks && cursor==-1);
    for(unsigned j=0;j<sizeof(out);j++)assert(out[j]==0xcc);
    f.at_reads=0;cursor=0;
    assert(lr_vfs_readv_at(&vfs,&f,read_at,&cursor,INT64_MAX-16,&iov,1)==-EINVAL);
    assert(!f.at_reads && !f.reads && !f.seeks);
    assert(!lr_vfs_readv_at(&vfs,&f,read_at,&cursor,INT64_MAX,NULL,0));
    assert(!f.at_reads);
    /* A malformed reply after a valid prefix must not zero or otherwise
     * publish the unfilled suffix, nor touch the legacy handle cursor. */
    f.invalid_at=3;f.limit=7;f.at_reads=0;f.position=57;cursor=57;
    for(unsigned j=0;j<64;j++)f.bytes[j]=(uint8_t)(j+1);
    memset(out,0xcc,sizeof(out));
    assert(lr_vfs_readv_at(&vfs,&f,read_at,&cursor,0,&iov,1)==-EIO);
    assert(cursor==-1 && !f.reads && !f.seeks && f.position==57);
    assert(!memcmp(out,f.bytes,14));
    for(unsigned j=14;j<sizeof(out);j++)assert(out[j]==0xcc);
    f.invalid_at=0;f.at_reads=0;
    assert(!lr_vfs_readv_at(&vfs,&f,read_at,&cursor,0,&iov,1));
    assert(!memcmp(out,f.bytes,sizeof(out)) && f.position==57 && cursor==-1);
    printf("VFS v6: %u short/EOF/decline/gap cases, no seek on positional reads; v3 negotiation and invalid replies PASS\n",cases);
}
