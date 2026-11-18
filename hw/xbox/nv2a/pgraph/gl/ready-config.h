/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_GL_READY_CONFIG_H
#define NV2A_GL_READY_CONFIG_H

#include <stdbool.h>

/* Advertises the connected, restart-latched frontend option to replay tests. */
#define PGRAPH_GL_READY_OPTION 1

/* Immutable once the first OpenGL content is configured. The frontend option
 * defaults off and needs a RetroArch restart after that first GL load. */
bool pgraph_gl_prepared_shaders_enabled(void);

/* Keep the historical compile-availability symbols for fixture compatibility.
 * These do not enable preparation: the restart-only frontend option defaults
 * off, and initialization also requires SSO and complete owner validation.
 * Developer builds can suppress compilation for a reference capture. */
#ifndef XEMU_DISABLE_READY_GL
#ifndef XEMU_EXPERIMENTAL_READY_VSH
#define XEMU_EXPERIMENTAL_READY_VSH 1
#endif
#ifndef XEMU_EXPERIMENTAL_READY_FF
#define XEMU_EXPERIMENTAL_READY_FF 1
#endif
/* Historical compile-default marker, not the runtime option default. */
#define PGRAPH_GL_READY_DEFAULT 1
#else
#define PGRAPH_GL_READY_DEFAULT 0
#endif

#if defined(XEMU_EXPERIMENTAL_READY_FF) && !defined(XEMU_EXPERIMENTAL_READY_VSH)
#error XEMU_EXPERIMENTAL_READY_FF requires XEMU_EXPERIMENTAL_READY_VSH
#endif
#endif
