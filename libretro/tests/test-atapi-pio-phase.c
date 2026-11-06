/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define BUSY_STAT 0x80
#define ERR_STAT 1
#define DRQ_STAT 8
#define ATAPI_INT_REASON_IO 2
#define ATAPI_SECTOR_BITS 11
#define ATAPI_SECTOR_SIZE 2048
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define trace_cd_read_sector_cb(...) ((void)0)
#define trace_cd_read_sector(...) ((void)0)
#define trace_ide_atapi_cmd_reply_end(...) ((void)0)
#define trace_ide_atapi_cmd_reply_end_bcl(...) ((void)0)
#define trace_ide_atapi_cmd_reply_end_new(...) ((void)0)
#define trace_ide_atapi_cmd_reply_end_eot(...) ((void)0)
typedef struct {void *data; size_t size;} QEMUIOVector;
typedef struct IDEState IDEState;
typedef void EndTransferFunc(IDEState *);
typedef struct {int unused;} BlockAIOCB;
struct IDEState {
    void *blk, *bus; int lba, cd_sector_size, io_buffer_size, io_buffer_index;
    int packet_transfer_size, elementary_transfer_size, io_buffer_total_len;
    int64_t nb_sectors; unsigned status,nsector,lcyl,hcyl;
    uint8_t *io_buffer, *data_ptr, *data_end;
    QEMUIOVector qiov; void (*end_transfer_func)(IDEState*);
    unsigned feature, end_transfer_fn_idx;
    int cur_io_buffer_offset, cur_io_buffer_len, atapi_dma;
    int req_nb_sectors;
    bool atapi_pio_pending;
    BlockAIOCB *pio_aiocb;
};
static struct {BlockAIOCB common;IDEState*s; void (*cb)(void*,int); void *opaque; QEMUIOVector q;
               int64_t sector; bool live;} request;
static unsigned reads, irqs, done, errors, phases; static bool immediate;
static size_t expected_cursor; static int expected_sector_size, first_lba;
void ide_atapi_cmd_reply_end(IDEState *s);
static void qemu_iovec_init_buf(QEMUIOVector *q,void*p,size_t n) {*q=(QEMUIOVector){p,n};}
static void ide_atapi_io_error(IDEState*s,int ret) {assert(ret<0);s->status=1;errors++;}
static void ide_atapi_cmd_ok(IDEState*s) {s->status=0;done++;}
static void ide_bus_set_irq(void*bus) {(void)bus;irqs++;}
static BlockAIOCB *ide_buffered_readv(IDEState*s,int64_t sector,QEMUIOVector*q,int n,
                              void(*cb)(void*,int),void*opaque)
{
    assert(!request.live && s->status & BUSY_STAT && !(s->status & DRQ_STAT));
    assert(n>0 && q->size==(size_t)n*512);reads++;
    request.s=s;request.cb=cb;request.opaque=opaque;request.q=*q;
    request.sector=sector;request.live=true;return &request.common;
}
static bool cancel_ready;
static void blk_aio_cancel(BlockAIOCB *a)
{
    assert(a==&request.common && request.live);
    void(*cb)(void*,int)=request.cb;void*arg=request.opaque;
    request.live=false;cb(arg,cancel_ready?0:-ECANCELED);
}
static uint8_t disk_byte(size_t off) {return (uint8_t)((off*13)^(off>>9)^(off>>17));}
static uint8_t expected(size_t off)
{
    size_t lba=first_lba+off/expected_sector_size, pos=off%expected_sector_size;
    if(expected_sector_size==2048) return disk_byte(lba*2048+pos);
    if(!pos || pos==11) return 0;
    if(pos<11) return 255;
    if(pos==12) return (uint8_t)((lba+150)/75/60);
    if(pos==13) return (uint8_t)(((lba+150)/75)%60);
    if(pos==14) return (uint8_t)((lba+150)%75);
    if(pos==15) return 1;
    if(pos>=2064) return 0;
    return disk_byte(lba*2048+pos-16);
}
static void consume(IDEState*s)
{
    size_t n=(size_t)(s->data_end-s->data_ptr);
    assert(n>0 && n<=(s->lcyl|(s->hcyl<<8)));
    unsigned before=reads; assert(s->status & DRQ_STAT);
    for(size_t i=0;i<n;i++) assert(s->data_ptr[i]==expected(expected_cursor+i));
    expected_cursor+=n;s->data_ptr=s->data_end;
    assert(reads==before); /* No provider access anywhere in an announced burst. */
}
static bool ide_transfer_start_norecurse(IDEState*s,uint8_t*p,int n,void(*end)(IDEState*))
{
    assert(n>0 && !(s->status & BUSY_STAT));phases++;
    s->data_ptr=p;s->data_end=p+n;s->end_transfer_func=end;s->status|=DRQ_STAT;
    if(immediate){consume(s);return true;}
    return false;
}
#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof(*(a))))
static void (*transfer_end_table[])(IDEState*)={ide_atapi_cmd_reply_end};
/* PRODUCTION */
static void finish(int ret)
{
    assert(request.live);IDEState*s=request.s;
    for(unsigned i=0;i<100;i++) assert((s->status & BUSY_STAT) && !(s->status & DRQ_STAT));
    if(!ret) for(size_t i=0;i<request.q.size;i++)
        ((uint8_t*)request.q.data)[i]=disk_byte((size_t)request.sector*512+i);
    void(*cb)(void*,int)=request.cb;void*arg=request.opaque;
    request.live=false;cb(arg,ret);
}
static void run(unsigned bcl,int sector_size,int sectors,bool adapter,bool pending_load)
{
    uint8_t buffer[131076];IDEState s={.io_buffer=buffer,.io_buffer_total_len=sizeof(buffer),
        .nb_sectors=80000,.lcyl=bcl&255,.hcyl=bcl>>8};
    memset(buffer,0xa5,sizeof(buffer));assert(!request.live);
    expected_cursor=0;expected_sector_size=sector_size;first_lba=121;
    unsigned old_done=done,old_errors=errors,old_phases=phases;
    unsigned old_reads=reads,old_irqs=irqs;immediate=adapter;
    ide_atapi_cmd_read_pio(&s,first_lba,sectors,sector_size);
    unsigned loops=0,replays=0;
    while(done==old_done && errors==old_errors){
        assert(++loops<1000000);
        if(request.live) {
            if(pending_load)for(unsigned repeat=0;repeat<3;repeat++){
                uint8_t saved_bytes[sizeof(buffer)];
                assert(s.atapi_pio_pending && !ide_drive_pio_pre_save(&s));
                IDEState saved=s;
                if(repeat==1){saved.lcyl=2;saved.hcyl=0;}
                unsigned saved_limit_lo=s.lcyl,saved_limit_hi=s.hcyl;
                memcpy(saved_bytes,buffer,sizeof(buffer));
                unsigned old_irqs=irqs,old_done=done,old_errors=errors;
                int64_t source=request.sector;size_t bytes=request.q.size;
                cancel_ready=repeat&1;
                ide_atapi_pio_cancel(&s);
                assert(!request.live && !s.atapi_pio_pending && !s.pio_aiocb);
                assert(irqs==old_irqs && done==old_done && errors==old_errors);
                s=saved;memcpy(buffer,saved_bytes,sizeof(buffer));
                s.data_ptr=s.data_end=NULL;s.end_transfer_func=NULL;
                s.pio_aiocb=NULL;s.qiov=(QEMUIOVector){NULL,0};
                unsigned before=reads;
                assert(!ide_drive_pio_post_load(&s,3));
                assert(reads==before && !request.live && s.atapi_pio_pending);
                ide_atapi_pio_resume(&s);replays++;
                assert(reads==before+1 && request.live && s.pio_aiocb);
                assert(request.sector==source && request.q.size==bytes);
                ide_atapi_pio_resume(&s);assert(reads==before+1);
                s.lcyl=saved_limit_lo;s.hcyl=saved_limit_hi;
            }
            finish(0);
        }
        else {
            assert(!immediate);
            /* Rebuild the host pointers as a savestate load does, after
             * partially consuming a phase. All guest bytes/cursors survive. */
            size_t half=(size_t)(s.data_end-s.data_ptr)/2;
            for(size_t i=0;i<half;i++) assert(s.data_ptr[i]==expected(expected_cursor+i));
            expected_cursor+=half;s.data_ptr+=half;
            s.cur_io_buffer_offset=(int)(s.data_ptr-s.io_buffer);
            s.cur_io_buffer_len=(int)(s.data_end-s.data_ptr);
            s.end_transfer_fn_idx=0;s.end_transfer_func=NULL;
            s.data_ptr=s.data_end=NULL;
            assert(!ide_drive_pio_post_load(&s,2));
            consume(&s);s.end_transfer_func(&s);
        }
    }
    assert(!request.live && errors==old_errors && done==old_done+1);
    assert(expected_cursor==(size_t)sectors*sector_size);
    unsigned cap=bcl==65535?65534:bcl;
    if((size_t)sectors*sector_size>cap) cap&=~1u;
    assert(phases-old_phases==((size_t)sectors*sector_size+cap-1)/cap);
    assert(irqs-old_irqs==phases-old_phases+1);
    assert(reads-old_reads-replays<=phases-old_phases);
}
int main(void)
{
    const unsigned limits[]={2,6,510,2048,2352,4094,4096,65533,65534,65535};
    for(unsigned i=0;i<sizeof(limits)/sizeof(*limits);i++)
        for(int raw=0;raw<2;raw++) for(int fast=0;fast<2;fast++)
            run(limits[i],raw?2352:2048,137,fast,false);
    for(unsigned raw=0;raw<2;raw++)for(unsigned odd=0;odd<2;odd++)
        run(odd?4094:65534,raw?2352:2048,137,false,true);
    uint8_t buf[131076];IDEState s={.io_buffer=buf,.io_buffer_total_len=sizeof(buf),
        .nb_sectors=100,.lcyl=0xfe,.hcyl=0xff};
    unsigned old_errors=errors; immediate=false;
    ide_atapi_cmd_read_pio(&s,0,2,2048);assert(request.live);finish(-EIO);
    assert(errors==old_errors+1 && !(s.status & DRQ_STAT));
    s.lcyl=0;s.hcyl=0;ide_atapi_cmd_read_pio(&s,0,2,2048);
    assert(errors==old_errors+2 && !request.live);
    s.end_transfer_fn_idx=0;s.cur_io_buffer_offset=0;
    s.cur_io_buffer_len=0;s.packet_transfer_size=1;
    assert(ide_drive_pio_post_load(&s,1)==-EINVAL);
    s.cur_io_buffer_offset=-1;
    assert(ide_drive_pio_post_load(&s,2)==-EINVAL);
    s.cur_io_buffer_offset=0;s.io_buffer_index=0;s.io_buffer_size=0;
    s.cd_sector_size=99;
    assert(ide_drive_pio_post_load(&s,2)==-EINVAL);
    for(unsigned bad=0;bad<13;bad++){
        s=(IDEState){.io_buffer=buf,.io_buffer_total_len=sizeof(buf),.nb_sectors=100,
            .lcyl=0xfe,.hcyl=0xff,.atapi_pio_pending=true,.cd_sector_size=2048,
            .status=BUSY_STAT,.packet_transfer_size=4096,.req_nb_sectors=2};
        switch(bad){
        case 0:s.status|=DRQ_STAT;break;
        case 1:s.feature=1;break;
        case 2:s.io_buffer_size=2048;break;
        case 3:s.lba=25;break;
        case 4:s.packet_transfer_size=0;break;
        case 5:s.cur_io_buffer_offset=-1;break;
        case 6:s.cur_io_buffer_len=1;break;
        case 7:s.end_transfer_fn_idx=99;break;
        case 8:s.pio_aiocb=&request.common;break;
        case 9:s.req_nb_sectors=0;break;
        case 10:s.req_nb_sectors=0x7fffffff;break;
        case 11:s.io_buffer_size=s.packet_transfer_size=1024;break;
        case 12:s.req_nb_sectors=3;break;
        }
        unsigned before=reads;
        assert(ide_drive_pio_post_load(&s,3)==-EINVAL && !s.atapi_pio_pending);
        ide_atapi_pio_resume(&s);assert(reads==before && !request.live);
    }
    s=(IDEState){.io_buffer=buf,.io_buffer_total_len=sizeof(buf),.nb_sectors=100,
        .atapi_pio_pending=true,.cd_sector_size=2048,.status=BUSY_STAT,
        .packet_transfer_size=4096,.req_nb_sectors=2};
    /* Changing the future phase limit to zero cannot alter an already
     * submitted extent. Its normal protocol error occurs after completion. */
    assert(!ide_drive_pio_post_load(&s,3) && s.atapi_pio_pending);
    old_errors=errors;ide_atapi_pio_resume(&s);assert(request.live);
    finish(0);assert(errors==old_errors+1 && !s.atapi_pio_pending && !s.pio_aiocb);
    puts("ATAPI PIO: delayed whole-phase byte/IRQ oracles, cooked/raw tails, immediate adapters and errors passed");
}
