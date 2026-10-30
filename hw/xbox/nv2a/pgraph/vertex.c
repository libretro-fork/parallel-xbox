/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/nv2a/nv2a_int.h"

void pgraph_update_inline_value(VertexAttribute *attr, const uint8_t *data)
{
    assert(attr->count <= 4);
    attr->inline_value[0] = 0.0f;
    attr->inline_value[1] = 0.0f;
    attr->inline_value[2] = 0.0f;
    attr->inline_value[3] = 1.0f;

    switch (attr->format) {
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
            for (uint32_t i = 0; i < attr->count; ++i) {
                attr->inline_value[i] = (float)data[i] / 255.0f;
            }
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1: {
            const int16_t *val = (const int16_t *) data;
            for (uint32_t i = 0; i < attr->count; ++i, ++val) {
                attr->inline_value[i] = MAX(-1.0f, (float) *val / 32767.0f);
            }
            break;
        }
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
            memcpy(attr->inline_value, data, attr->size * attr->count);
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K: {
            const int16_t *val = (const int16_t *) data;
            for (uint32_t i = 0; i < attr->count; ++i, ++val) {
                attr->inline_value[i] = (float)*val;
            }
            break;
        }
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP: {
            /* 3 signed, normalized components packed in 32-bits. (11,11,10) */
            const int32_t val = *(const int32_t *)data;
            int32_t x = val & 0x7FF;
            if (x & 0x400) {
                x |= 0xFFFFF800;
            }
            int32_t y = (val >> 11) & 0x7FF;
            if (y & 0x400) {
                y |= 0xFFFFF800;
            }
            int32_t z = (val >> 22) & 0x7FF;
            if (z & 0x200) {
                z |= 0xFFFFFC00;
            }

            attr->inline_value[0] = MAX(-1.0f, (float)x / 1023.0f);
            attr->inline_value[1] = MAX(-1.0f, (float)y / 1023.0f);
            attr->inline_value[2] = MAX(-1.0f, (float)z / 511.0f);
            break;
        }
    default:
        fprintf(stderr, "Unknown vertex attribute type: for format 0x%x\n",
                attr->format);
        assert(!"Unsupported attribute type");
        break;
    }
}

void pgraph_get_inline_values(PGRAPHState *pg, uint16_t attrs,
                               float values[NV2A_VERTEXSHADER_ATTRIBUTES][4],
                               int *count)
{
    int num_attributes = 0;

    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        if (attrs & (1 << i)) {
            memcpy(values[num_attributes],
                   pg->vertex_attributes[i].inline_value, 4 * sizeof(float));
            num_attributes += 1;
        }
    }

    if (count) {
        *count = num_attributes;
    }
}


void pgraph_allocate_inline_buffer_vertices(PGRAPHState *pg, unsigned int attr)
{
    VertexAttribute *attribute = &pg->vertex_attributes[attr];

    if (attribute->inline_buffer_populated || pg->inline_buffer_length == 0) {
        return;
    }

    /* Now upload the previous attribute value */
    attribute->inline_buffer_populated = true;
    for (int i = 0; i < pg->inline_buffer_length; i++) {
        memcpy(&attribute->inline_buffer[i * 4], attribute->inline_value,
               sizeof(float) * 4);
    }
}

void pgraph_finish_inline_buffer_vertex(PGRAPHState *pg)
{
    pgraph_check_within_begin_end_block(pg);
    assert(pg->inline_buffer_length < NV2A_MAX_BATCH_LENGTH);

    for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &pg->vertex_attributes[i];
        if (attribute->inline_buffer_populated) {
            memcpy(&attribute->inline_buffer[pg->inline_buffer_length * 4],
                   attribute->inline_value, sizeof(float) * 4);
        }
    }

    pg->inline_buffer_length++;
}

/* DRAW_ARRAYS mixed with explicit indices: endpoints provide the bounds
 * while generating the required index list, without scanning it later. */
void pgraph_append_inline_elements_range(PGRAPHState *pg, uint32_t start,
                                          unsigned int count)
{
    assert((size_t)pg->inline_elements_length + count < NV2A_MAX_BATCH_LENGTH);
    if (!count) {
        return;
    }
    uint32_t end = start + count - 1;
    pgraph_update_inline_elements_bounds(pg, end < start ? 0 : start,
                                         end < start ? UINT32_MAX : end);
    uint32_t *out = pg->inline_elements + pg->inline_elements_length;
    for (unsigned int i = 0; i < count; i++) {
        out[i] = start + i;
    }
    pg->inline_elements_length += count;
}

/* Legacy states contain only the canonical list. Rebuild once on load,
 * before a restored partial primitive can be flushed or extended. */
void pgraph_rebuild_inline_elements_bounds(PGRAPHState *pg)
{
    uint32_t low = UINT32_MAX, high = 0;
    for (unsigned int i = 0; i < pg->inline_elements_length; i++) {
        low = MIN(low, pg->inline_elements[i]);
        high = MAX(high, pg->inline_elements[i]);
    }
    pg->inline_elements_min = low;
    pg->inline_elements_max = high;
}

void pgraph_reset_inline_buffers(PGRAPHState *pg)
{
    pg->inline_elements_length = 0;
    pg->inline_elements_min = UINT32_MAX;
    pg->inline_elements_max = 0;
    pg->inline_array_length = 0;
    pg->inline_buffer_length = 0;
    pgraph_reset_draw_arrays(pg);
}

void pgraph_reset_draw_arrays(PGRAPHState *pg)
{
    pg->draw_arrays_length = 0;
    pg->draw_arrays_min_start = -1;
    pg->draw_arrays_max_count = 0;
    pg->draw_arrays_prevent_connect = false;
}
