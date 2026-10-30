/*
 * Trace points.
 *
 * The core builds no trace backend: every generated trace_*() is an empty
 * inline (tracetool's nop backend) and whether one is enabled is a
 * constant false, so a check costs nothing at run time.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TRACE__CONTROL_H
#define TRACE__CONTROL_H

#define trace_event_get_state(id)           false
#define trace_event_get_state_backends(id)  false

#endif /* TRACE__CONTROL_H */
