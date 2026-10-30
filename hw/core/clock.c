/*
 * Hardware Clocks
 *
 * Copyright GreenSocs 2016-2020
 *
 * Authors:
 *  Frederic Konrad
 *  Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/visitor.h"
#include "system/qtest.h"
#include "hw/clock.h"
#include "trace.h"

#define CLOCK_PATH(_clk) (_clk->canonical_path)

void clock_setup_canonical_path(Clock *clk)
{
    g_free(clk->canonical_path);
    clk->canonical_path = object_get_canonical_path(OBJECT(clk));
}

void clock_set_callback(Clock *clk, ClockCallback *cb, void *opaque,
                        unsigned int events)
{
    assert(OBJECT(clk)->parent);
    clk->callback = cb;
    clk->callback_opaque = opaque;
    clk->callback_events = events;
}

bool clock_set(Clock *clk, uint64_t period)
{
    if (clk->period == period) {
        return false;
    }
    trace_clock_set(CLOCK_PATH(clk), CLOCK_PERIOD_TO_HZ(clk->period),
                    CLOCK_PERIOD_TO_HZ(period));
    clk->period = period;

    return true;
}

static void clock_disconnect(Clock *clk)
{
    if (clk->source == NULL) {
        return;
    }

    trace_clock_disconnect(CLOCK_PATH(clk));

    clk->source = NULL;
    QLIST_REMOVE(clk, sibling);
}

static void clock_period_prop_get(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    Clock *clk = CLOCK(obj);
    uint64_t period = clock_get(clk);
    visit_type_uint64(v, name, &period, errp);
}

static void clock_unparent(Object *obj)
{
    /*
     * Callback are registered by the parent, which might die anytime after
     * it's unparented the children.  Avoid having a callback to a deleted
     * object in case the clock is still referenced somewhere else (eg: by
     * a clock output).
     */
    clock_set_callback(CLOCK(obj), NULL, NULL, 0);
}

static void clock_initfn(Object *obj)
{
    Clock *clk = CLOCK(obj);

    clk->multiplier = 1;
    clk->divider = 1;

    QLIST_INIT(&clk->children);

    if (qtest_enabled()) {
        object_property_add(obj, "qtest-clock-period", "uint64",
                            clock_period_prop_get, NULL, NULL, NULL);
    }
}

static void clock_finalizefn(Object *obj)
{
    Clock *clk = CLOCK(obj);
    Clock *child, *next;

    /* clear our list of children */
    QLIST_FOREACH_SAFE(child, &clk->children, sibling, next) {
        clock_disconnect(child);
    }

    /* remove us from source's children list */
    clock_disconnect(clk);

    g_free(clk->canonical_path);
}

static void clock_class_init(ObjectClass *klass, const void *data)
{
    klass->unparent = clock_unparent;
}

static const TypeInfo clock_info = {
    .name              = TYPE_CLOCK,
    .parent            = TYPE_OBJECT,
    .instance_size     = sizeof(Clock),
    .instance_init     = clock_initfn,
    .class_init        = clock_class_init,
    .instance_finalize = clock_finalizefn,
};

static void clock_register_types(void)
{
    type_register_static(&clock_info);
}

type_init(clock_register_types)
