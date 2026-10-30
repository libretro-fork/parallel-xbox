/*
 * Generic FIFO component, implemented as a circular buffer.
 *
 * Copyright (c) 2012 Peter A. G. Crosthwaite
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/fifo8.h"

void fifo8_reset(Fifo8 *fifo)
{
    fifo->num = 0;
    fifo->head = 0;
}

void fifo8_create(Fifo8 *fifo, uint32_t capacity)
{
    fifo->data = g_new(uint8_t, capacity);
    fifo->capacity = capacity;
    fifo8_reset(fifo);
}

void fifo8_destroy(Fifo8 *fifo)
{
    g_free(fifo->data);
}

void fifo8_push(Fifo8 *fifo, uint8_t data)
{
    assert(fifo->num < fifo->capacity);
    fifo->data[(fifo->head + fifo->num) % fifo->capacity] = data;
    fifo->num++;
}

uint8_t fifo8_pop(Fifo8 *fifo)
{
    uint8_t ret;

    assert(fifo->num > 0);
    ret = fifo->data[fifo->head++];
    fifo->head %= fifo->capacity;
    fifo->num--;
    return ret;
}

bool fifo8_is_empty(Fifo8 *fifo)
{
    return (fifo->num == 0);
}

bool fifo8_is_full(Fifo8 *fifo)
{
    return (fifo->num == fifo->capacity);
}

const VMStateDescription vmstate_fifo8 = {
    .name = "Fifo8",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(data, Fifo8, 1, NULL, capacity),
        VMSTATE_UINT32(head, Fifo8),
        VMSTATE_UINT32(num, Fifo8),
        VMSTATE_END_OF_LIST()
    }
};
