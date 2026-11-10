/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "hw/xbox/mcpx/nvnet/nvnet_regs.h"
#define QEMU_PACKED __attribute__((packed))
/* RING */
typedef uint64_t dma_addr_t;
typedef struct {bool pending;unsigned mods,dels;} Timer;
typedef struct {uint32_t regs[256],phy_regs[32];Timer *rx_poll_timer;void *nic;unsigned irqs;} NvNetState;
typedef NvNetState PCIDevice;
typedef struct {NvNetState *opaque;bool link_down,tx_blocked;} NetClientState;
#define PCI_DEVICE(s) (s)
#define MII_BMSR 1
#define MII_BMSR_LINK_ST 4
#define RX_POLL_INTERVAL_MS 1
#define QEMU_CLOCK_VIRTUAL 0
#define GET_MASK(v,m) (((v)&(m))>>__builtin_ctz(m))
#define le32_to_cpu(v) (v)
#define le16_to_cpu(v) (v)
#define cpu_to_le32(v) (v)
#define cpu_to_le16(v) (v)
#define NVNET_DPRINTF(...) do {if(0)printf(__VA_ARGS__);}while(0)
#define trace_nvnet_cant_rx(...) ((void)0)
#define trace_nvnet_rx_dma(...) ((void)0)
#define trace_nvnet_desc_store(...) ((void)0)
static uint8_t ram[65536];static unsigned reads,writes;
static bool running=true;static unsigned flushes;
static bool runstate_is_running(void){return running;}
static void qemu_flush_queued_packets(NetClientState *nc){(void)nc;flushes++;}
static uint32_t get_reg(NvNetState*s,uint32_t reg){assert(reg%4==0&&reg<1024);return s->regs[reg/4];}
static void set_reg(NvNetState*s,uint32_t reg,uint32_t value){assert(reg%4==0&&reg<1024);s->regs[reg/4]=value;}
static void or_reg(NvNetState*s,uint32_t reg,uint32_t value){set_reg(s,reg,get_reg(s,reg)|value);}
static void and_reg(NvNetState*s,uint32_t reg,uint32_t value){set_reg(s,reg,get_reg(s,reg)&value);}
static NetClientState *qemu_get_queue(void *nic){return nic;}
static NvNetState *qemu_get_nic_opaque(NetClientState *nc){return nc->opaque;}
static bool timer_pending(Timer*t){return t->pending;}
static int64_t qemu_clock_get_ms(int clock){assert(clock==0);return 100;}
static void timer_mod(Timer*t,int64_t when){assert(when==101);t->pending=true;t->mods++;}
static void timer_del(Timer*t){t->pending=false;t->dels++;}
static void pci_dma_read(PCIDevice*d,dma_addr_t address,void*out,size_t size){assert(address+size<=sizeof(ram)&&size==8);reads++;memcpy(out,ram+address,size);}
static void pci_dma_write(PCIDevice*d,dma_addr_t address,const void*in,size_t size){assert(address+size<=sizeof(ram));writes++;memcpy(ram+address,in,size);}
static void set_intr_status(NvNetState*s,uint32_t status){assert(status==NVNET_IRQ_STATUS_RX);s->irqs++;}
/* PRODUCTION */
int main(void){
 static uint8_t expected[65536],payload[2048];unsigned cases=0;
 const unsigned sizes[]={0,1,2,63,1500,2048};
 for(unsigned enable=0;enable<8;enable++)for(unsigned available=0;available<2;available++)
 for(unsigned position=0;position<6;position++)for(unsigned sz=0;sz<6;sz++)
 for(unsigned pending=0;pending<2;pending++)for(unsigned overlap=0;overlap<2;overlap++){
  Timer timer={.pending=pending};NvNetState s={.rx_poll_timer=&timer};NetClientState nc={.opaque=&s};s.nic=&nc;
  const uint32_t base=128,count=5,end=base+count*8;
  uint32_t next=position<4?base+position*8:position==4?base-8:end;
  uint32_t current=next<base||next+8>end?base:next;
  unsigned size=sizes[sz];
  for(unsigned i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)(i*37+cases);
  for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(i*29+sz);
  struct RingDesc desc={.buffer_addr=overlap?current:4096,.length=size?size-1:0,.flags=available?NV_RX_AVAIL:0};
  memcpy(ram+current,&desc,sizeof(desc));
  set_reg(&s,NVNET_RX_RING_PHYS_ADDR,base);set_reg(&s,NVNET_RING_SIZE,(count-1)<<16);
  set_reg(&s,NVNET_RX_RING_NEXT_DESC_PHYS_ADDR,next);set_reg(&s,NVNET_RX_RING_CURRENT_DESC_PHYS_ADDR,0xabcdef);
  set_reg(&s,NVNET_RECEIVER_CONTROL,enable&1?NVNET_RECEIVER_CONTROL_START:0);
  set_reg(&s,NVNET_TX_RX_CONTROL,NVNET_TX_RX_CONTROL_IDLE|(enable&2?0:NVNET_TX_RX_CONTROL_BIT2));
  s.phy_regs[MII_BMSR]=enable&4?MII_BMSR_LINK_ST:0;
  bool ready=enable==7,deliver=ready&&available;
  /* Backend readiness must never provide a cached descriptor for a later
   * callback: change the payload destination after probing it. */
  if(ready&&available&&!overlap){
   assert(nvnet_can_receive(&nc));desc.buffer_addr=8192;memcpy(ram+current,&desc,sizeof(desc));
   timer=(Timer){.pending=pending};
  }
  memcpy(expected,ram,sizeof(ram));reads=writes=0;
  ssize_t result=dma_packet_to_guest(&s,payload,size);
  assert(result==(deliver?(ssize_t)size:-1));
  assert(reads==(ready?1u:0u));assert(writes==(deliver?2u:0u));assert(s.irqs==deliver);
  if(deliver){
   memcpy(expected+desc.buffer_addr,payload,size);desc.length=size;desc.flags=NV_RX_BIT4|NV_RX_DESCRIPTORVALID;
   memcpy(expected+current,&desc,sizeof(desc));
  }
  assert(!memcmp(ram,expected,sizeof(ram)));
  assert(get_reg(&s,NVNET_RX_RING_CURRENT_DESC_PHYS_ADDR)==(ready?current:0xabcdef));
  assert(get_reg(&s,NVNET_RX_RING_NEXT_DESC_PHYS_ADDR)==(deliver?(current+8>=end?base:current+8):next));
  assert(get_reg(&s,NVNET_TX_RX_CONTROL)&NVNET_TX_RX_CONTROL_IDLE);
  assert(timer.pending==(ready&&!available));
  assert(timer.mods==(ready&&!available&&!pending));assert(timer.dels==!(ready&&!available));cases++;
 }
 printf("PASS: %u RX deliveries/readiness cases; one descriptor read per enabled receive, fresh callback snapshots, payload/descriptor overlap, ring updates, IRQ and timer state\n",cases);
 unsigned gates=0;
 for(unsigned enable=0;enable<8;enable++)for(unsigned available=0;available<2;available++)
 for(unsigned stopped=0;stopped<2;stopped++)for(unsigned down=0;down<2;down++)
 for(unsigned pending=0;pending<2;pending++)for(unsigned tx=0;tx<2;tx++)
 for(unsigned poll=0;poll<2;poll++){
  Timer timer={.pending=pending};NvNetState s={.rx_poll_timer=&timer};
  NetClientState nc={.opaque=&s,.link_down=down,.tx_blocked=tx};s.nic=&nc;
  struct RingDesc desc={.buffer_addr=4096,.length=1499,.flags=available?NV_RX_AVAIL:0};
  memcpy(ram+128,&desc,sizeof(desc));memcpy(expected,ram,sizeof(ram));
  set_reg(&s,NVNET_RX_RING_PHYS_ADDR,128);
  set_reg(&s,NVNET_RX_RING_NEXT_DESC_PHYS_ADDR,128);
  set_reg(&s,NVNET_RX_RING_CURRENT_DESC_PHYS_ADDR,0xabcdef);
  set_reg(&s,NVNET_RECEIVER_CONTROL,enable&1?NVNET_RECEIVER_CONTROL_START:0);
  set_reg(&s,NVNET_TX_RX_CONTROL,enable&2?0:NVNET_TX_RX_CONTROL_BIT2);
  s.phy_regs[MII_BMSR]=enable&4?MII_BMSR_LINK_ST:0;
  running=!stopped;reads=writes=flushes=0;
  /* Both the MMIO path and poll callback must enforce the same gate. */
  if(poll)rx_poll_timer(&s);else nvnet_flush_rx(&s);
  bool active=!stopped&&!down,ready=enable==7;
  assert(reads==(active&&ready));assert(!writes&&!s.irqs);
  assert(flushes==(active&&ready&&available));
  assert(timer.pending==(active?(ready&&!available):pending));
  assert(timer.mods==(active&&ready&&!available&&!pending));
  assert(timer.dels==(active&&!(ready&&!available)));
  assert(get_reg(&s,NVNET_RX_RING_CURRENT_DESC_PHYS_ADDR)==(active&&ready?128:0xabcdef));
  assert(!memcmp(ram,expected,sizeof(ram)));
  gates++;
 }
 printf("PASS: %u RX flush/poll gates including stopped VM, link-down, full ring and TX backpressure\n",gates);
}
