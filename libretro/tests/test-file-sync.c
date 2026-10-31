/* Real POSIX file backend with only its sync syscall fault-injected. */
#include "block/libretro-file.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned calls, interruptions;
static int terminal_error;

int test_sync(int fd)
{
    calls++;
    if (interruptions) {
        interruptions--;
        errno = EINTR;
        return -1;
    }
    if (terminal_error) {
        errno = terminal_error;
        return -1;
    }
#ifdef __linux__
    return fdatasync(fd);
#else
    return fsync(fd);
#endif
}

int main(int argc, char **argv)
{
    const char data[] = "native sync interruption regression";
    char readback[sizeof(data)];
    assert(argc == 2);
    LRFile *f = lr_file_open(argv[1], true, true);
    assert(f && lr_file_pwrite(f, 0, data, sizeof(data)) == 0);
    interruptions = 2;
    assert(lr_file_sync(f) == 0 && calls == 3);

    calls = 0;
    terminal_error = EIO;
    assert(lr_file_sync(f) == -EIO && calls == 1);
    terminal_error = 0;
    assert(lr_file_sync(f) == 0 && calls == 2);

    calls = 0;
    interruptions = 2;
    lr_file_close(f);
    assert(calls == 3); /* close must reach a real successful sync too */

    f = lr_file_open(argv[1], false, false);
    assert(f && lr_file_pread(f, 0, readback, sizeof(readback)) == 0);
    assert(!memcmp(data, readback, sizeof(data)));
    calls = 0;
    lr_file_close(f);
    assert(calls == 0); /* a read-only close needs no sync */
    puts("native sync: EINTR retries, EIO propagation, close and readback: PASS");
    return 0;
}
