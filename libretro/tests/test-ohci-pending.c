/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define USB_MAX_ENDPOINTS 15
#define ED_LINK_LIMIT 32
/* DEFINITIONS */
typedef struct USBDevice USBDevice;
typedef struct USBPort { USBDevice *dev; char path[16]; void *opaque; } USBPort;
typedef struct { unsigned nr; int pid; USBDevice *dev; } USBEndpoint;
struct USBDevice { unsigned addr; bool attached; USBPort *port; USBEndpoint ep_ctl, ep_in[15], ep_out[15]; };
struct iovec { void *iov_base; size_t iov_len; };
typedef struct { struct iovec *iov; size_t size; int niov, nalloc; } QEMUIOVector;
typedef struct { int status, actual_length, pid; uint8_t *data; size_t len; bool inflight; USBEndpoint *ep; QEMUIOVector iov; } USBPacket;
typedef struct OHCIState OHCIState;
/* PACKET TYPES */
struct OHCIState {
    uint32_t done, intr_status;
    int done_count;
    OHCIEndpointSlot *packets;
    unsigned packet_count, packet_capacity, servicing, num_ports;
    struct { USBPort port; } rhport[4];
};
enum { DEVICES = 4, DESCRIPTORS = 128 };
static USBDevice devices[DEVICES];
static USBPort ports[DEVICES];
static uint8_t ram[1024 * 1024];
static struct ohci_td descriptors[DESCRIPTORS];
static struct ohci_ed endpoints[DESCRIPTORS];
static unsigned reads, writes, ed_writes, copies_in, copies_out, setups, handles;
static unsigned flushes, finds, deaths, cancels, stopped, dispatches, allocations, live;
static int result;
static bool copy_fault, descriptor_fault, complete_on_flush;
static void *allocate(size_t n) { void *p = malloc(n); assert(p); memset(p,0xa5,n); allocations++; live++; return p; }
static void release(void *p) { if (p) { assert(live); live--; free(p); } }
static void *renew(void *p, size_t n) { if (!p) return allocate(n); p = realloc(p,n); assert(p); allocations++; return p; }
static void *zeroed(size_t n) { void *p = allocate(n); memset(p,0,n); return p; }
#define g_malloc allocate
#define g_free release
#define g_renew(t,p,n) ((t *)renew(p,sizeof(t)*(n)))
#define g_new0(t,n) ((t *)zeroed(sizeof(t)*(n)))
#define g_new(t,n) ((t *)allocate(sizeof(t)*(n)))
/* IOVEC */
static void usb_packet_init(USBPacket *p) { qemu_iovec_init(&p->iov, 1); }
static bool usb_packet_is_inflight(USBPacket *p) { return p->inflight; }
static void usb_cancel_packet(USBPacket *p) { assert(p->inflight); p->inflight = false; cancels++; }
static void usb_device_ep_stopped(USBDevice *d, USBEndpoint *ep) { assert(ep->dev == d); stopped++; }
static void ohci_clear_packets(OHCIState *s);
static void ohci_die(OHCIState *s) { deaths++; ohci_clear_packets(s); s->intr_status |= OHCI_INTR_UE; }
static unsigned td_index(uint32_t a) { assert(a && a%0x100 == 0 && a/0x100 <= DESCRIPTORS); return a/0x100-1; }
static unsigned ed_index(uint32_t a) { assert(a >= 0x100000 && (a-0x100000)%16 == 0); unsigned i=(a-0x100000)/16; assert(i<DESCRIPTORS); return i; }
static int ohci_read_td(OHCIState *s, uint32_t a, struct ohci_td *td) { reads++; if (descriptor_fault) return -1; *td=descriptors[td_index(a)]; return 0; }
static int ohci_put_td(OHCIState *s, uint32_t a, struct ohci_td *td) { writes++; descriptors[td_index(a)]=*td; return 0; }
static int ohci_read_ed(OHCIState *s, uint32_t a, struct ohci_ed *ed) { *ed=endpoints[ed_index(a)]; return 0; }
static int ohci_put_ed(OHCIState *s, uint32_t a, struct ohci_ed *ed) { endpoints[ed_index(a)]=*ed; ed_writes++; return 0; }
static int ohci_service_iso_td(OHCIState *s, struct ohci_ed *ed) { abort(); }
enum { DMA_DIRECTION_TO_DEVICE, DMA_DIRECTION_FROM_DEVICE };
static int ohci_copy_td(OHCIState *s, struct ohci_td *td, uint8_t *data, size_t n, int dir)
{
    if (copy_fault) return -1;
    assert(td->cbp+n <= sizeof(ram));
    if (dir == DMA_DIRECTION_TO_DEVICE) { copies_out++; memcpy(data,ram+td->cbp,n); }
    else { copies_in++; memcpy(ram+td->cbp,data,n); }
    return 0;
}
static USBDevice *ohci_find_device(OHCIState *s, unsigned a)
{ finds++; for (unsigned i=0;i<DEVICES;i++) if (devices[i].attached && devices[i].addr==a) return &devices[i]; return NULL; }
static USBEndpoint *usb_ep_get(USBDevice *d, int pid, unsigned n)
{ assert(n<16); return !n ? &d->ep_ctl : pid==USB_TOKEN_IN ? &d->ep_in[n-1] : &d->ep_out[n-1]; }
static void usb_packet_setup(USBPacket *p,int pid,USBEndpoint *ep,int stream,uint32_t id,bool short_not_ok,bool int_req)
{ assert(!p->inflight && p->iov.iov); setups++; p->status=USB_RET_SUCCESS; p->actual_length=0; p->pid=pid; p->ep=ep; qemu_iovec_reset(&p->iov); }
static void usb_packet_addbuf(USBPacket *p,void *data,size_t n) { p->data=data; p->len=n; qemu_iovec_add(&p->iov,data,n); }
static void usb_handle_packet(USBDevice *d, USBPacket *p)
{ handles++; p->status=result; p->actual_length=result==USB_RET_SUCCESS?p->len:0; p->inflight=result==USB_RET_ASYNC; if (result==USB_RET_SUCCESS && p->pid==USB_TOKEN_IN) memset(p->data,0x69,p->len); }
static void ohci_async_complete_packet(USBPort *port, USBPacket *p);
static void ohci_process_lists(OHCIState *s) { assert(!s->servicing); dispatches++; }
static void usb_device_flush_ep_queue(USBDevice *d,USBEndpoint *ep);
#define ohci_td_pkt(...) ((void)0) /* Legacy candidates only. */
/* PRODUCTION */
static void usb_device_flush_ep_queue(USBDevice *d,USBEndpoint *ep)
{
    flushes++;
    if (complete_on_flush) {
        OHCIState *s=ports[0].opaque;
        for (unsigned i=0;i<s->packet_count;i++) if (s->packets[i].ep==ep) {
            USBPacket *p=&s->packets[i].packet->usb_packet;
            p->status=USB_RET_SUCCESS; p->actual_length=p->len; p->inflight=false;
            if (p->pid==USB_TOKEN_IN) memset(p->data,0x69,p->len);
            ohci_async_complete_packet(NULL,p); return;
        }
        abort();
    }
}
static void reset(OHCIState *s)
{
    ohci_clear_packets(s); assert(!live);
    memset(s,0,sizeof(*s)); s->done_count=7;
    memset(ram,0x3c,sizeof(ram)); memset(descriptors,0,sizeof(descriptors)); memset(endpoints,0,sizeof(endpoints));
    reads=writes=ed_writes=copies_in=copies_out=setups=handles=flushes=finds=deaths=cancels=stopped=dispatches=allocations=0;
    result=USB_RET_ASYNC; copy_fault=descriptor_fault=complete_on_flush=false;
    for (unsigned i=0;i<DEVICES;i++) {
        devices[i].addr=i+1; devices[i].attached=true; devices[i].port=&ports[i];
        ports[i].dev=&devices[i]; ports[i].opaque=s; snprintf(ports[i].path,sizeof(ports[i].path),"%u",i+1);
        devices[i].ep_ctl=(USBEndpoint){.dev=&devices[i]};
        for (unsigned n=0;n<15;n++) {
            devices[i].ep_in[n]=(USBEndpoint){n+1,USB_TOKEN_IN,&devices[i]};
            devices[i].ep_out[n]=(USBEndpoint){n+1,USB_TOKEN_OUT,&devices[i]};
        }
    }
}
static struct ohci_ed transfer(unsigned slot,unsigned dev,unsigned dir,unsigned ep,unsigned n,bool td_direction)
{
    descriptors[slot]=(struct ohci_td){.flags=OHCI_TD_R|(dir<<OHCI_TD_DP_SHIFT),
        .cbp=n?0x10000u+slot*0x2000u:0,.next=0xfff000,.be=n?0x10000u+slot*0x2000u+n-1:0};
    endpoints[slot]=(struct ohci_ed){.head=(slot+1)*0x100,
        .flags=dev|(ep<<OHCI_ED_EN_SHIFT)|(64u<<OHCI_ED_MPS_SHIFT)|(td_direction?0:dir<<OHCI_ED_D_SHIFT),.tail=0xfff000};
    return endpoints[slot];
}
static void complete(OHCIState *s,unsigned td,unsigned n)
{
    OHCIEndpointPacket *p=ohci_pending_td(s,td); assert(p);
    memset(p->buffer,0x69,MIN(n,p->buffer_size));
    p->usb_packet.status=USB_RET_SUCCESS; p->usb_packet.actual_length=n; p->usb_packet.inflight=false;
    ohci_async_complete_packet(NULL,&p->usb_packet);
}
int main(void)
{
    OHCIState s={0}; unsigned scenarios=0;
    QEMUIOVector vector; uint8_t bytes[4]={0};
    qemu_iovec_init(&vector,1); qemu_iovec_add(&vector,bytes,1); qemu_iovec_add(&vector,bytes+1,2);
    assert(vector.niov==2 && vector.size==3 && vector.iov[1].iov_base==bytes+1);
    qemu_iovec_reset(&vector); assert(!vector.niov && !vector.size);
    qemu_iovec_destroy(&vector); qemu_iovec_destroy(&vector);
    assert(!vector.iov && !vector.nalloc && !live); scenarios++;
    /* Control endpoint zero is shared by SETUP/OUT/IN. Its pending payload
     * must remain owned even when completion is awaiting TD writeback. */
    for (unsigned dir=1;dir<=2;dir++) for (unsigned n=1;n<=64;n++)
    for (unsigned incoming=0;incoming<=2;incoming++) for (unsigned td_dir=0;td_dir<2;td_dir++) {
        reset(&s); struct ohci_ed first=transfer(0,1,dir,0,n,td_dir);
        assert(ohci_service_td(&s,&first)==1 && handles==1 && flushes==1);
        OHCIEndpointPacket *p=ohci_pending_td(&s,0x100); assert(p && p->buffer_size==64);
        assert(ohci_service_td(&s,&first)==1 && reads==1 && handles==1);
        memset(p->buffer,0x69,64); uint8_t original[64]; memcpy(original,p->buffer,64);
        struct ohci_ed second=transfer(1,1,incoming,0,8192,td_dir);
        for (unsigned done=0;done<2;done++) {
            if (done) complete(&s,0x100,n);
            unsigned old_copies=copies_out,old_alloc=allocations;
            assert(ohci_service_td(&s,&second)==1 && !memcmp(original,p->buffer,64));
            assert(copies_out==old_copies && allocations==old_alloc && handles==1);
        }
        assert(!ohci_service_td(&s,&first) && !ohci_pending_td(&s,0x100));
        assert((first.head&OHCI_DPTR_MASK)==0xfff000 && writes==1);
        if (dir==OHCI_TD_DIR_IN) { assert(copies_in==1); for (unsigned i=0;i<n;i++) assert(ram[0x10000+i]==0x69); }
        second=transfer(1,1,OHCI_TD_DIR_OUT,0,n,td_dir); result=USB_RET_SUCCESS;
        assert(!ohci_service_td(&s,&second) && handles==2 && writes==2 && !deaths); scenarios++;
    }
    /* Forty independent endpoints exercise table growth and reverse-order
     * completion. Callback packet addresses remain stable as keys move. */
    reset(&s); OHCIEndpointPacket *stable[40]; struct ohci_ed eds[40];
    for (unsigned i=0;i<40;i++) {
        eds[i]=transfer(i,i/30+1,i%2+1,(i%30)/2+1,32,i&1);
        assert(ohci_service_td(&s,&eds[i])==1);
        stable[i]=ohci_pending_td(&s,(i+1)*0x100); assert(stable[i]);
    }
    assert(s.packet_count==40 && handles==40); unsigned busy_alloc=allocations;
    for (unsigned i=0;i<40;i++) assert(stable[i]==ohci_pending_td(&s,(i+1)*0x100) && stable[i]->buffer_size==64);
    for (unsigned i=40;i-->0;) { complete(&s,(i+1)*0x100,32); assert(!ohci_service_td(&s,&eds[i])); }
    assert(allocations==busy_alloc && handles==40 && writes==40); scenarios+=40;
    for (unsigned i=0;i<2000;i++) {
        struct ohci_ed ed=transfer(0,1,OHCI_TD_DIR_OUT,1,32,i&1); result=USB_RET_SUCCESS;
        assert(!ohci_service_td(&s,&ed) && allocations==busy_alloc); scenarios++;
    }
    reset(&s); struct ohci_ed growth=transfer(0,1,2,1,16,false);
    assert(ohci_service_td(&s,&growth)); complete(&s,0x100,16); assert(!ohci_service_td(&s,&growth));
    growth=transfer(0,1,2,1,8192,false); assert(ohci_service_td(&s,&growth));
    OHCIEndpointPacket *grown=ohci_pending_td(&s,0x100); assert(grown->buffer_size==8192);
    for (unsigned i=0;i<8192;i++) assert(grown->buffer[i]==0xa5); /* no stale-payload copy */
    complete(&s,0x100,8192); assert(!ohci_service_td(&s,&growth));
    busy_alloc=allocations; result=USB_RET_SUCCESS; growth=transfer(0,1,2,1,1,false);
    assert(!ohci_service_td(&s,&growth) && allocations==busy_alloc); scenarios++;
    /* Completion while flushing is consumed in this service pass. */
    for (unsigned dir=1;dir<=2;dir++) {
        reset(&s); complete_on_flush=true; transfer(0,1,dir,1,16,false);
        assert(ohci_service_ed_list(&s,0x100000)==1);
        assert(!dispatches && !s.servicing && writes==1 && ed_writes==1 && !ohci_pending_td(&s,0x100)); scenarios++;
    }
    reset(&s); struct ohci_ed a=transfer(0,1,1,1,32,false),b=transfer(1,1,2,1,32,false);
    assert(ohci_service_td(&s,&a) && ohci_service_td(&s,&b)); endpoints[0].flags|=OHCI_ED_K;
    ohci_service_ed_list(&s,0x100000); assert(cancels==1 && !ohci_pending_td(&s,0x100) && ohci_pending_td(&s,0x200)); scenarios++;
    devices[0].addr=9; complete(&s,0x200,32); assert(!ohci_service_td(&s,&b)); scenarios++;
    /* Hub detach/reset preserves root 10 when removing root 1 and 1.3. */
    reset(&s); strcpy(ports[1].path,"1.3"); strcpy(ports[2].path,"10");
    for (unsigned i=0;i<3;i++) { eds[i]=transfer(i,i+1,2,1,32,false); assert(ohci_service_td(&s,&eds[i])); }
    OHCIEndpointPacket *moved=ohci_pending_td(&s,0x300);
    ohci_drop_device_packets(&s,&devices[0]); assert(s.packet_count==1 && cancels==2 && moved->slot==0);
    complete(&s,0x300,32); assert(!ohci_service_td(&s,&eds[2])); scenarios++;
    /* Rewind cancels host requests and ignores obsolete single-slot data. */
    reset(&s);
    for (unsigned i=0;i<3;i++) { eds[i]=transfer(i,1,2,i+1,32,false); assert(ohci_service_td(&s,&eds[i])); }
    complete(&s,0x300,32); assert(!ohci_pre_load(&s) && cancels==2 && !live && !s.packets && !s.packet_count);
    assert(ohci_service_td(&s,&eds[0]) && ohci_pending_td(&s,0x100)); scenarios++;
    const int statuses[]={USB_RET_SUCCESS,USB_RET_NAK,USB_RET_STALL,USB_RET_IOERROR,USB_RET_NODEV,USB_RET_BABBLE};
    for (unsigned dir=1;dir<=2;dir++) for (unsigned i=0;i<sizeof(statuses)/sizeof(*statuses);i++) {
        reset(&s); result=statuses[i]; struct ohci_ed ed=transfer(0,1,dir,1,32,false);
        assert(ohci_service_td(&s,&ed)==(result!=USB_RET_SUCCESS));
        unsigned cc=OHCI_BM(descriptors[0].flags,TD_CC);
        assert(cc==(result==USB_RET_STALL?OHCI_CC_STALL:result==USB_RET_BABBLE?OHCI_CC_DATAOVERRUN:result==USB_RET_NODEV||result==USB_RET_IOERROR?OHCI_CC_DEVICENOTRESPONDING:0));
        assert(writes==(result!=USB_RET_NAK)); scenarios++;
        reset(&s); ed=transfer(0,1,dir,1,32,false); assert(ohci_service_td(&s,&ed));
        OHCIEndpointPacket *p=ohci_pending_td(&s,0x100); assert(p);
        p->usb_packet.status=statuses[i]; p->usb_packet.actual_length=statuses[i]==USB_RET_SUCCESS?32:0;
        p->usb_packet.inflight=false; ohci_async_complete_packet(NULL,&p->usb_packet);
        assert(ohci_service_td(&s,&ed)==(statuses[i]!=USB_RET_SUCCESS));
        assert(!ohci_pending_td(&s,0x100) && writes==(statuses[i]!=USB_RET_NAK)); scenarios++;
    }
    const unsigned lengths[]={0,1,63,64,65,128,4095,4096,8192};
    for (unsigned dir=1;dir<=2;dir++) for (unsigned i=0;i<sizeof(lengths)/sizeof(*lengths);i++) {
        reset(&s); result=USB_RET_SUCCESS; unsigned n=lengths[i]; struct ohci_ed ed=transfer(0,1,dir,1,n,false);
        assert(!ohci_service_td(&s,&ed) && handles==1 && writes==1);
        unsigned requested=dir==1?MIN(n,64):n;
        assert(s.packets[0].packet->buffer_size==MAX(requested,64)); scenarios++;
    }
    reset(&s); a=transfer(0,1,2,1,16,false); assert(ohci_service_td(&s,&a)); complete(&s,0x100,65);
    assert(ohci_service_td(&s,&a) && !copies_in && OHCI_BM(descriptors[0].flags,TD_CC)==OHCI_CC_DATAOVERRUN); scenarios++;
    for (unsigned dir=1;dir<=2;dir++) {
        reset(&s); result=USB_RET_SUCCESS; copy_fault=true; a=transfer(0,1,dir,1,16,false);
        assert(ohci_service_td(&s,&a) && deaths==1 && !live && !s.packet_count && !writes); scenarios++;
    }
    reset(&s); a=transfer(0,1,2,1,32,false); assert(ohci_service_td(&s,&a)); descriptor_fault=true;
    b=transfer(1,1,1,2,32,false); assert(ohci_service_td(&s,&b) && deaths==1 && cancels==1 && !live); scenarios++;
    reset(&s); struct ohci_ed invalid={0}; assert(ohci_service_td(&s,&invalid) && deaths==1 && !reads && !handles); scenarios++;
    reset(&s); result=USB_RET_SUCCESS; copy_fault=true;
    transfer(0,1,1,1,16,false); transfer(1,1,1,2,16,false); endpoints[0].next=0x100010;
    assert(!ohci_service_ed_list(&s,0x100000) && deaths==1 && !handles && !ed_writes && !live); scenarios++;
    /* Reusing a TD still on the done chain can form a persistent cycle. */
    reset(&s); result=USB_RET_SUCCESS; transfer(0,1,1,1,0,false);
    descriptors[0].next=0x100; s.done=0x100;
    assert(ohci_service_ed_list(&s,0x100000)==1);
    assert(handles==TD_SERVICE_LIMIT && writes==TD_SERVICE_LIMIT && ed_writes==1 && !deaths && !s.servicing); scenarios++;
    ohci_clear_packets(&s); ohci_clear_packets(&s); assert(!live);
    printf("OHCI endpoints: %u ownership/concurrency/reuse/callback/reset/rewind/error scenarios passed; 40 simultaneous endpoints, zero steady-state allocations\n",scenarios);
}
