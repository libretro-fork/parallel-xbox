#include "qemu/osdep.h"
#include "keymaps.h"
#include "ui/input.h"

#include "standard-headers/linux/input.h"

#include "ui/input-keymap-atset1-to-qcode.c.inc"
#include "ui/input-keymap-linux-to-qcode.c.inc"
#include "ui/input-keymap-qcode-to-atset1.c.inc"
#include "ui/input-keymap-qcode-to-atset2.c.inc"
#include "ui/input-keymap-qcode-to-atset3.c.inc"
#include "ui/input-keymap-qcode-to-linux.c.inc"
#include "ui/input-keymap-qcode-to-qnum.c.inc"
#include "ui/input-keymap-qcode-to-sun.c.inc"
#include "ui/input-keymap-qnum-to-qcode.c.inc"
#include "ui/input-keymap-usb-to-qcode.c.inc"
#include "ui/input-keymap-win32-to-qcode.c.inc"
#include "ui/input-keymap-x11-to-qcode.c.inc"
#include "ui/input-keymap-xorgevdev-to-qcode.c.inc"
#include "ui/input-keymap-xorgkbd-to-qcode.c.inc"
#include "ui/input-keymap-xorgxquartz-to-qcode.c.inc"
#include "ui/input-keymap-xorgxwin-to-qcode.c.inc"
#include "ui/input-keymap-osx-to-qcode.c.inc"

int qemu_input_key_number_to_qcode(unsigned int nr)
{
    if (nr >= qemu_input_map_qnum_to_qcode_len) {
        return 0;
    }
    return qemu_input_map_qnum_to_qcode[nr];
}

