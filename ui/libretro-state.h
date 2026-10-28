/*
 * The core's savestates (libretro-state.c)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UI_LIBRETRO_STATE_H
#define UI_LIBRETRO_STATE_H

/* The most a state can take for the machine as built */
size_t lr_state_size(void);
bool lr_state_save(void *data, size_t size, Error **errp);
bool lr_state_load(const void *data, size_t size, bool *disk_newer,
                   Error **errp);
void lr_state_drop_legacy(void);

#endif
