/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "qemu/queue.h"
#include "hw/xbox/mcpx/nvnet/nvnet_regs.h"
#ifdef _WIN32
struct iovec {void *iov_base;size_t iov_len;};
#else
#include <sys/uio.h>
#endif
#define g_malloc malloc
#define g_free free
typedef struct {int unused;} NetClientState;
typedef struct NetPacket NetPacket;
typedef struct NetQueue NetQueue;
typedef void NetPacketSent(NetClientState *,ssize_t);
typedef ssize_t NetQueueDeliverFunc(NetClientState *,unsigned,const struct iovec *,int,void *);
/* QUEUE TYPES */
static bool blocked,can_send=true;
static bool qemu_can_send_packet(NetClientState *sender){return can_send;}
bool qemu_net_queue_flush(NetQueue *queue);
/* QUEUE */
#define QEMU_PACKED __attribute__((packed))
/* RING */
typedef uint64_t hwaddr;
typedef struct {int unused;} AddressSpace;
typedef struct {uint32_t tx_dma_buf_offset;uint8_t tx_dma_buf[TX_ALLOC_BUFSIZE];unsigned next;bool enabled,idle;unsigned irq;} NvNetState;
typedef NvNetState PCIDevice;
#define PCI_DEVICE(s) (s)
#define MEMTXATTRS_UNSPECIFIED 0
#define NVNET_DPRINTF(...) do {if(0)printf(__VA_ARGS__);}while(0)
#define trace_nvnet_tx_dma(...) ((void)0)
#define RING_BASE 1024u
static uint8_t ram[4096],delivered[2048];
static size_t delivered_size,dma_bytes;
static unsigned map_calls,unmaps,borrows,sends,outstanding;
static bool reject_map;
static NetQueue packets;
static NetClientState sender;
static AddressSpace address_space;
static bool can_transmit(NvNetState*s){return s->enabled;}
static void set_dma_idle(NvNetState*s,bool idle){s->idle=idle;}
static uint32_t get_reg(NvNetState*s,unsigned reg){assert(reg==NVNET_TX_RING_PHYS_ADDR);return RING_BASE;}
static uint16_t get_tx_ring_size(NvNetState*s){return 8;}
static uint32_t update_current_tx_ring_desc_addr(NvNetState*s){return RING_BASE+s->next*sizeof(struct RingDesc);}
static struct RingDesc load_ring_desc(NvNetState*s,uint32_t a){struct RingDesc d;memcpy(&d,ram+a,sizeof(d));return d;}
static void store_ring_desc(NvNetState*s,uint32_t a,struct RingDesc d){assert(!outstanding);memcpy(ram+a,&d,sizeof(d));}
static void advance_next_tx_ring_desc_addr(NvNetState*s){s->next=(s->next+1)%8;}
static void set_intr_status(NvNetState*s,unsigned irq){s->irq|=irq;}
static AddressSpace *pci_get_address_space(PCIDevice*d){return &address_space;}
static void *address_space_map_direct(AddressSpace*as,hwaddr a,hwaddr*n,bool write,int attrs){map_calls++;assert(as==&address_space&&!write);if(reject_map||!*n||a+*n>sizeof(ram)){*n=0;return NULL;}outstanding++;return ram+a;}
static void address_space_unmap(AddressSpace*as,void*p,hwaddr n,bool write,hwaddr used){assert(outstanding==1&&n==used&&!write);outstanding--;unmaps++;}
static void pci_dma_read(PCIDevice*d,uint32_t a,void*out,size_t n){assert(a+n<=sizeof(ram));dma_bytes+=n;memcpy(out,ram+a,n);}
static ssize_t deliver(NetClientState*nc,unsigned flags,const struct iovec*iov,int count,void*opaque){assert(count==1);if(blocked)return 0;assert(iov[0].iov_len<=sizeof(delivered));delivered_size=iov[0].iov_len;memcpy(delivered,iov[0].iov_base,delivered_size);return delivered_size?delivered_size:1;}
static void send_packet(NvNetState*s,const uint8_t*buf,size_t n){sends++;if(outstanding){assert(buf>=ram&&buf<ram+sizeof(ram));borrows++;}else assert(buf==s->tx_dma_buf);qemu_net_queue_send(&packets,&sender,0,buf,n,NULL);}
/* TX */
static NvNetState nic;
static void reset(void){assert(!outstanding);assert(QTAILQ_EMPTY(&packets.packets));memset(&nic,0,sizeof(nic));nic.enabled=true;memset(ram,0,sizeof(ram));for(unsigned i=0;i<256;i++)ram[128+i]=(uint8_t)(i*13+7);memset(delivered,0,sizeof(delivered));delivered_size=dma_bytes=0;map_calls=unmaps=borrows=sends=0;blocked=reject_map=false;can_send=true;packets=(NetQueue){.nq_maxlen=10,.deliver=deliver};QTAILQ_INIT(&packets.packets);}
static void desc(unsigned index,unsigned a,unsigned n,bool last){struct RingDesc d={a,n-1,NV_TX_VALID|(last?NV_TX_LASTPACKET:0)};memcpy(ram+RING_BASE+index*sizeof(d),&d,sizeof(d));}
static void complete(void){assert(!outstanding&&nic.idle&&nic.irq==NVNET_IRQ_STATUS_TX&&sends==1);assert(!(load_ring_desc(&nic,RING_BASE).flags&NV_TX_VALID));}
int main(void){
 QTAILQ_INIT(&packets.packets);
 uint8_t expected[2048];
 reset();desc(0,128,64,true);memcpy(expected,ram+128,64);dma_packet_from_guest(&nic);complete();assert(borrows==1&&unmaps==1&&!dma_bytes&&delivered_size==64&&!memcmp(delivered,expected,64));
 const unsigned sizes[]={1,1500,TX_ALLOC_BUFSIZE};
 for(unsigned k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
  unsigned n=sizes[k];
  reset();for(unsigned i=0;i<n;i++)ram[2048+i]=(uint8_t)(i*29+3);
  desc(0,2048,n,true);memcpy(expected,ram+2048,n);dma_packet_from_guest(&nic);complete();assert(borrows==1&&unmaps==1&&!dma_bytes&&delivered_size==n&&!memcmp(delivered,expected,n));
 }
 /* Backpressure, recursion and cannot-send all force production queue copies. */
 for(unsigned mode=0;mode<3;mode++){
  reset();desc(0,128,64,true);memcpy(expected,ram+128,64);blocked=mode==0;packets.delivering=mode==1;can_send=mode!=2;
  dma_packet_from_guest(&nic);complete();assert(borrows==1&&unmaps==1&&!dma_bytes&&packets.nq_count==1);
  memset(ram+128,0xee,64);blocked=false;packets.delivering=0;can_send=true;assert(qemu_net_queue_flush(&packets));assert(!packets.nq_count&&delivered_size==64&&!memcmp(delivered,expected,64));
 }
 reset();reject_map=true;desc(0,128,64,true);memcpy(expected,ram+128,64);dma_packet_from_guest(&nic);complete();assert(map_calls==1&&!borrows&&!unmaps&&dma_bytes==64&&!memcmp(delivered,expected,64));
 reset();blocked=true;packets.nq_maxlen=0;desc(0,128,64,true);dma_packet_from_guest(&nic);complete();assert(borrows==1&&unmaps==1&&!packets.nq_count&&!dma_bytes);
 /* A fragmented packet spans invocations and retains its earlier bytes. */
 reset();desc(0,128,7,false);memcpy(expected,ram+128,7);dma_packet_from_guest(&nic);assert(nic.tx_dma_buf_offset==7&&!sends&&!map_calls&&!nic.irq);
 memset(ram+128,0xee,7);desc(1,200,13,true);memcpy(expected+7,ram+200,13);dma_packet_from_guest(&nic);assert(sends==1&&!map_calls&&dma_bytes==20&&!nic.tx_dma_buf_offset&&delivered_size==20&&!memcmp(delivered,expected,20));
 /* Payload may alias its descriptor; completion must follow consumption. */
 reset();desc(0,RING_BASE,sizeof(struct RingDesc),true);memcpy(expected,ram+RING_BASE,sizeof(struct RingDesc));dma_packet_from_guest(&nic);complete();assert(!memcmp(delivered,expected,sizeof(struct RingDesc)));
 reset();nic.enabled=false;desc(0,128,64,true);dma_packet_from_guest(&nic);assert(!map_calls&&!sends&&load_ring_desc(&nic,RING_BASE).flags&NV_TX_VALID);
 reset();dma_packet_from_guest(&nic);assert(!map_calls&&!sends&&nic.idle&&!nic.irq);
 puts("NVNet TX: borrowed pointer/no staging copy, queued lifetime, descriptor alias, fallback, fragmented snapshots and completion ordering PASS");
 return 0;
}
