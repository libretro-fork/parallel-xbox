/* SPDX-License-Identifier: GPL-2.0-or-later
 * Force-include only when compiling a private translate-all test object.
 * Release sources and objects never include this header. */
#include "qemu/osdep.h"
#include "exec/translation-block.h"
static void lr_test_capture_code(uint64_t pc, uint64_t guest_size,
                                uint64_t insns, uint64_t cflags,
                                uint64_t flags, uint64_t code_size,
                                const void *code)
{
    static int fd = -2;
    uint64_t header[6] = {pc, guest_size, insns, cflags, flags, code_size};
    if (pc < 0xfff00000 || pc >= 0xfff10000) { return; }
    if (fd == -2) {
        const char *path = getenv("XEMU_TCG_CAPTURE");
        int mode = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_BINARY
        mode |= O_BINARY;
#endif
        fd = path ? open(path, mode, 0600) : -1;
        if (path && fd < 0) { abort(); }
    }
    if (fd < 0) { return; }
    /* One CPU owner writes this test-only stream, without staging code. */
    if (write(fd, header, sizeof(header)) != sizeof(header) ||
        write(fd, code, code_size) != code_size) { abort(); }
}
#define LIBRETRO_TCG_CAPTURE(pc, tb, start) \
    lr_test_capture_code((pc), (tb)->size, (tb)->icount, tb_cflags(tb), \
                         (tb)->flags, (tb)->tc.size, (start))
