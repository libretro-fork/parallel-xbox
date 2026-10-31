/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * QEMU UI Console
 */
#ifndef CONSOLE_PRIV_H
#define CONSOLE_PRIV_H

#include "ui/console.h"
#include "qemu/timer.h"

#include "vgafont.h"

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

struct QemuConsole {
    Object parent;

    int index;
    DisplayState *ds;
    DisplaySurface *surface;
    DisplayScanout scanout;
    int dcls;
    DisplayGLCtx *gl;
    int gl_block;
    QEMUTimer *gl_unblock_timer;
    int window_id;
    QemuUIInfo ui_info;
    QEMUTimer *ui_timer;
    const GraphicHwOps *hw_ops;
    void *hw;

    QTAILQ_ENTRY(QemuConsole) next;
};

#endif
