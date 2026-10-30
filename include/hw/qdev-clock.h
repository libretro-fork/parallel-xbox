/*
 * Device's clock input and output
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

#ifndef QDEV_CLOCK_H
#define QDEV_CLOCK_H

#include "hw/clock.h"

/**
 * qdev_finalize_clocklist:
 * @dev: the device being finalized
 *
 * Clear the clocklist from @dev. Only used internally in qdev.
 */
void qdev_finalize_clocklist(DeviceState *dev);

/**
 * ClockPortInitElem:
 * @name: name of the clock (can't be NULL)
 * @output: indicates whether the clock is input or output
 * @callback: for inputs, optional callback to be called on clock's update
 * with device as opaque
 * @callback_events: mask of ClockEvent values for when callback is called
 * @offset: optional offset to store the ClockIn or ClockOut pointer in device
 * state structure (0 means unused)
 */
struct ClockPortInitElem {
    const char *name;
    bool is_output;
    ClockCallback *callback;
    unsigned int callback_events;
    size_t offset;
};

#define clock_offset_value(devstate, field) \
    (offsetof(devstate, field) + \
     type_check(Clock *, typeof_field(devstate, field)))

#define QDEV_CLOCK(out_not_in, devstate, field, cb, cbevents) {  \
    .name = (stringify(field)), \
    .is_output = out_not_in, \
    .callback = cb, \
    .callback_events = cbevents, \
    .offset = clock_offset_value(devstate, field), \
}

/**
 * QDEV_CLOCK_(IN|OUT):
 * @devstate: structure type. @dev argument of qdev_init_clocks below must be
 * a pointer to that same type.
 * @field: a field in @_devstate (must be Clock*)
 * @callback: (for input only) callback (or NULL) to be called with the device
 * state as argument
 * @cbevents: (for input only) ClockEvent mask for when callback is called
 *
 * The name of the clock will be derived from @field
 */
#define QDEV_CLOCK_IN(devstate, field, callback, cbevents)       \
    QDEV_CLOCK(false, devstate, field, callback, cbevents)

#define QDEV_CLOCK_OUT(devstate, field) \
    QDEV_CLOCK(true, devstate, field, NULL, 0)

#define QDEV_CLOCK_END { .name = NULL }

typedef struct ClockPortInitElem ClockPortInitArray[];

#endif /* QDEV_CLOCK_H */
