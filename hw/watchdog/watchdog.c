/*
 * Virtual hardware watchdog.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * By Richard W.M. Jones (rjones@redhat.com).
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/queue.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-run-state.h"
#include "qapi/qapi-events-run-state.h"
#include "system/runstate.h"
#include "system/watchdog.h"
#include "hw/nmi.h"
#include "qemu/help_option.h"
#include "trace.h"

