/*
 * QEMU PC speaker emulation
 *
 * Copyright (c) 2006 Joachim Henke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "qemu/module.h"
#include "hw/timer/i8254.h"
#include "migration/vmstate.h"
#include "hw/audio/pcspk.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "trace.h"

/*
 * Port 0x61: PIT channel 2's gate and output and the refresh toggle, which
 * guests time short delays with. The speaker's tone itself is not made:
 * the core has nowhere to play it.
 */

OBJECT_DECLARE_SIMPLE_TYPE(PCSpkState, PC_SPEAKER)

struct PCSpkState {
    ISADevice parent_obj;

    MemoryRegion ioport;
    uint32_t iobase;
    PITCommonState *pit;
    uint8_t data_on;
    uint8_t dummy_refresh_clock;
    bool migrate;
};

static uint64_t pcspk_io_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    PCSpkState *s = opaque;
    PITChannelInfo ch;
    uint8_t val;

    pit_get_channel_info(s->pit, 2, &ch);

    s->dummy_refresh_clock ^= (1 << 4);

    val = ch.gate | (s->data_on << 1) | s->dummy_refresh_clock |
       (ch.out << 5);

    trace_pcspk_io_read(s->iobase, val);

    return val;
}

static void pcspk_io_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    PCSpkState *s = opaque;
    const int gate = val & 1;

    trace_pcspk_io_write(s->iobase, val);

    s->data_on = (val >> 1) & 1;
    pit_set_gate(s->pit, 2, gate);
}

static const MemoryRegionOps pcspk_io_ops = {
    .read = pcspk_io_read,
    .write = pcspk_io_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pcspk_initfn(Object *obj)
{
    PCSpkState *s = PC_SPEAKER(obj);

    memory_region_init_io(&s->ioport, OBJECT(s), &pcspk_io_ops, s, "pcspk", 1);
}

static void pcspk_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    PCSpkState *s = PC_SPEAKER(dev);

    if (!s->pit) {
        error_setg(errp, "pcspk: No \"pit\" set or available");
        return;
    }

    isa_register_ioport(isadev, &s->ioport, s->iobase);
}

static bool migrate_needed(void *opaque)
{
    PCSpkState *s = opaque;

    return s->migrate;
}

static const VMStateDescription vmstate_spk = {
    .name = "pcspk",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = migrate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(data_on, PCSpkState),
        VMSTATE_UINT8(dummy_refresh_clock, PCSpkState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property pcspk_properties[] = {
    DEFINE_PROP_UINT32("iobase", PCSpkState, iobase,  0x61),
    DEFINE_PROP_BOOL("migrate", PCSpkState, migrate,  true),
    DEFINE_PROP_LINK("pit", PCSpkState, pit, TYPE_PIT_COMMON, PITCommonState *),
};

static void pcspk_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pcspk_realizefn;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->vmsd = &vmstate_spk;
    device_class_set_props(dc, pcspk_properties);
    /* Reason: pit object link */
    dc->user_creatable = false;
}

static const TypeInfo pcspk_info = {
    .name           = TYPE_PC_SPEAKER,
    .parent         = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(PCSpkState),
    .instance_init  = pcspk_initfn,
    .class_init     = pcspk_class_initfn,
};

static void pcspk_register(void)
{
    type_register_static(&pcspk_info);
}
type_init(pcspk_register)
