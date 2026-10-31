/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define TYPE_SMBUS_CX25871 "smbus-cx25871"
#define SMBUS_CX25871(p) ((SMBusCX25871Device *)(p))
#define DPRINTF(...) ((void)0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define NV_PCRTC_INTR_0_VBLANK 1
#define NV2A_VBLANK_INTERVAL_NS 16683333
#define QEMU_CLOCK_VIRTUAL 0
#define QEMU_CLOCK_REALTIME 1
typedef struct {uint8_t transfer_state[40];} SMBusDevice;
typedef void DeviceState;
typedef void Error;
typedef struct {size_t offset,size;} VMStateField;
typedef struct {const char *name;int version_id,minimum_version_id;const VMStateField *fields;} VMStateDescription;
#define VMSTATE_SMBUS_DEVICE(m,t) {offsetof(t,m),sizeof(SMBusDevice)}
#define VMSTATE_UINT8_ARRAY(m,t,n) {offsetof(t,m),n}
#define VMSTATE_UINT8(m,t) {offsetof(t,m),1}
#define VMSTATE_END_OF_LIST() {0,0}
typedef struct {
    void (*realize)(DeviceState *,Error **);void (*unrealize)(DeviceState *);const VMStateDescription *vmsd;
    void (*quick_cmd)(SMBusDevice *,uint8_t);uint8_t (*receive_byte)(SMBusDevice *);int (*write_data)(SMBusDevice *,uint8_t *,uint8_t);
} ObjectClass;
typedef ObjectClass DeviceClass;
typedef ObjectClass SMBusDeviceClass;
#define DEVICE_CLASS(p) (p)
#define SMBUS_DEVICE_CLASS(p) (p)
typedef struct {unsigned unused;} VGACommonState;
typedef struct {VGACommonState vga;struct {uint32_t pending_interrupts;} pcrtc;int64_t vblank_vt,vblank_deadline;} NV2AState;
static NV2AState gpu;
static int64_t clocks[2];static bool running;static unsigned irqs;static int expected_counter=-1;
static int64_t qemu_clock_get_ns(int clock) {assert(clock<2);return clocks[clock];}
static void retro_atomic_store_relaxed_64(int64_t *p,int64_t v) {*p=v;}
static bool runstate_is_running(void) {return running;}
/* DECLARATIONS */
static void nv2a_update_irq(NV2AState *d) {
    assert(d->pcrtc.pending_interrupts&1);
    if(expected_counter>=0) assert(conexant_device && conexant_device->registers[6]==expected_counter);
    irqs++;
}
/* PRODUCTION */
static void graphic_hw_update(void *con) {nv2a_vga_gfx_update(con);}
/* FRAME DISPATCH */
static size_t save(const VMStateDescription *desc,void *device,uint8_t *out) {
    size_t n=0;
    for(const VMStateField *f=desc->fields;f->size;f++) {memcpy(out+n,(char *)device+f->offset,f->size);n+=f->size;}
    return n;
}
static size_t load(const VMStateDescription *desc,void *device,const uint8_t *in) {
    size_t n=0;
    for(const VMStateField *f=desc->fields;f->size;f++) {memcpy((char *)device+f->offset,in+n,f->size);n+=f->size;}
    return n;
}
int main(void) {
    ObjectClass klass={0};smbus_cx25871_class_init(&klass,NULL);
    assert(klass.realize && klass.unrealize && klass.quick_cmd && klass.receive_byte && klass.write_data && klass.vmsd);
    assert(klass.vmsd->version_id==1 && klass.vmsd->minimum_version_id==1);
    smbus_cx25871_notify_vblank();SMBusCX25871Device cx,other;memset(&cx,0xa5,sizeof(cx));
    klass.realize(&cx,NULL);assert(conexant_device==&cx && !cx.cmd);
    for(unsigned i=0;i<256;i++) assert(!cx.registers[i]);
    unsigned cases=0;
    for(unsigned initial=0;initial<256;initial++) for(unsigned pending=0;pending<2;pending++) {
        uint8_t write[]={6,initial};klass.write_data(&cx.smbusdev,write,2);
        gpu.pcrtc.pending_interrupts=pending?1:0;
        for(unsigned frame=1;frame<=64;frame++) {
            bool done=false;running=false;clocks[0]=100;unsigned previous_irqs=irqs;
            expected_counter=(initial&0xf0)|((initial+frame)&15);
            dispatch_frame_vblank(&done,50,150);assert(!done && irqs==previous_irqs);
            assert(cx.registers[6]==((initial&0xf0)|((initial+frame-1)&15)));
            running=true;clocks[0]=49;dispatch_frame_vblank(&done,50,150);assert(!done && irqs==previous_irqs);
            clocks[0]=50;clocks[1]=123456;dispatch_frame_vblank(&done,50,150);
            assert(done && irqs==previous_irqs+1 && gpu.vblank_vt==50 && gpu.vblank_deadline==123456+NV2A_VBLANK_INTERVAL_NS);
            for(unsigned again=0;again<3;again++) dispatch_frame_vblank(&done,50,150);
            assert(irqs==previous_irqs+1);
            uint8_t select=6;klass.write_data(&cx.smbusdev,&select,1);
            assert(klass.receive_byte(&cx.smbusdev)==((initial&0xf0)|((initial+frame)&15)));
            assert(cx.cmd==7);cases++;
        }
    }
    /* Walk the production VMState descriptor, including parent SMBus
     * transaction bytes, every encoder register and the read cursor. */
    for(unsigned pass=0;pass<1000;pass++) {
        for(unsigned i=0;i<sizeof(cx);i++) ((uint8_t *)&cx)[i]=(uint8_t)(pass+i*31);
        SMBusCX25871Device expected=cx;uint8_t snapshot[sizeof(cx)];
        assert(save(klass.vmsd,&cx,snapshot)==sizeof(cx));memset(&cx,0,sizeof(cx));
        assert(load(klass.vmsd,&cx,snapshot)==sizeof(cx));assert(!memcmp(&cx,&expected,sizeof(cx)));
        unsigned cursor=cx.cmd;assert(klass.receive_byte(&cx.smbusdev)==expected.registers[cursor]);
        assert(cx.cmd==(uint8_t)(cursor+1));smbus_cx25871_notify_vblank();
        assert(cx.registers[6]==((expected.registers[6]&0xf0)|((expected.registers[6]+1)&15)));
    }
    klass.unrealize(&cx);assert(!conexant_device);smbus_cx25871_notify_vblank();
    klass.realize(&other,NULL);klass.unrealize(&cx);assert(conexant_device==&other);
    smbus_cx25871_notify_vblank();assert(other.registers[6]==1);klass.unrealize(&other);assert(!conexant_device);
    printf("Conexant: %u pause/vblank/IRQ/wrap/SMBus cases and 1000 VMState-field round trips; detach/recreate passed\n",cases);
}
