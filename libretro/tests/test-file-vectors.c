/* Production backend, including its immutable mapped lane without a VFS SDK.
 * Only the small mapped byte view is modeled; native file I/O is real. */
#define LR_FILE_FILESTREAM 0
#include "block/libretro-file.c"
#include <assert.h>
#include <stdio.h>

static void rejected_scalars(LRFile *f, bool writable)
{
    uint8_t out[32];
    memset(out, 0xcc, sizeof(out));
    assert(lr_file_pread(f, -1, out, 0) == -EINVAL);
    assert(lr_file_pread(f, INT64_MAX, out, 1) == -EINVAL);
    assert(lr_file_pread(f, INT64_MAX - 15, out, 16) == -EINVAL);
    if (SIZE_MAX > INT64_MAX) {
        assert(lr_file_pread(f, 0, out, SIZE_MAX) == -EINVAL);
    }
    assert(lr_file_pread(f, INT64_MAX, NULL, 0) == 0);
    for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
    if (writable) {
        assert(lr_file_pwrite(f, -1, out, 0) == -EINVAL);
        assert(lr_file_pwrite(f, INT64_MAX, out, 1) == -EINVAL);
        assert(lr_file_pwrite(f, INT64_MAX - 15, out, 16) == -EINVAL);
        if (SIZE_MAX > INT64_MAX) {
            assert(lr_file_pwrite(f, 0, out, SIZE_MAX) == -EINVAL);
        }
        assert(lr_file_pwrite(f, INT64_MAX, NULL, 0) == 0);
    } else {
        assert(lr_file_pwrite(f, INT64_MAX, out, 1) == -EPERM);
    }
}

static void rejected_reads(LRFile *f)
{
    uint8_t out[32];
    LRFileIOV overflow[] = {{out, 1}, {out + 16, SIZE_MAX}};
    LRFileIOV empty[] = {{NULL, 0}, {out, 0}};
    memset(out, 0xcc, sizeof(out));
    if (SIZE_MAX > INT64_MAX) {
        assert(lr_file_preadv(f, 0, overflow, 2) == -EINVAL);
    }
    assert(lr_file_preadv(f, INT64_MAX - 1, overflow, 2) == -EINVAL);
    assert(lr_file_preadv(f, 0, NULL, -1) == -EINVAL);
    assert(lr_file_preadv(f, 0, NULL, 1) == -EINVAL);
    assert(lr_file_preadv(f, -1, NULL, 0) == -EINVAL);
    assert(lr_file_preadv(f, INT64_MAX, NULL, 0) == 0);
    assert(lr_file_preadv(f, 0, empty, 2) == 0);
    for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    uint8_t bytes[64], out[64], source[64];
    memset(bytes, 0x5a, sizeof(bytes));
    memset(source, 0xa5, sizeof(source));
    LRFile mapped = {.mapped = bytes, .mapped_size = sizeof(bytes)};
    rejected_reads(&mapped);
    rejected_scalars(&mapped, false);
    LRFileIOV v[] = {{out, 8}, {out + 16, 8}};
    assert(lr_file_preadv(&mapped, 0, v, 2) == 0);
    assert(!memcmp(out, bytes, 8) && !memcmp(out + 16, bytes + 8, 8));
    assert(lr_file_pwritev(&mapped, 0, NULL, 0) == -EPERM);

    LRFile *f = lr_file_open(argv[1], true, true);
    assert(f && lr_file_set_size(f, sizeof(bytes)) == 0);
    assert(lr_file_pwrite(f, 0, bytes, sizeof(bytes)) == 0);
    rejected_reads(f);
    rejected_scalars(f, true);
    /* Force the non-preadv fallback as well as the native vectored lane. */
    LRFileIOV invalid[1300] = {{source, 1}, {source + 16, SIZE_MAX}};
    for (int count = 2; count <= 1300; count += 1298) {
        int64_t offset = SIZE_MAX > INT64_MAX ? 0 : INT64_MAX - 1;
        assert(lr_file_pwritev(f, offset, invalid, count) == -EINVAL);
        memset(out, 0xcc, sizeof(out));
        invalid[0].base = out;
        invalid[1].base = out + 16;
        assert(lr_file_preadv(f, offset, invalid, count) == -EINVAL);
        for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
        invalid[0].base = source;
        invalid[1].base = source + 16;
    }
    assert(lr_file_pwritev(f, 0, NULL, -1) == -EINVAL);
    assert(lr_file_pwritev(f, 0, NULL, 1) == -EINVAL);
    assert(lr_file_pwritev(f, -1, NULL, 0) == -EINVAL);
    assert(lr_file_pwritev(f, INT64_MAX, NULL, 0) == 0);
    assert(lr_file_size(f) == sizeof(bytes));
    assert(lr_file_pread(f, 0, out, sizeof(out)) == 0);
    assert(!memcmp(out, bytes, sizeof(bytes)));
    LRFileIOV valid[] = {{source, 7}, {source + 16, 9}, {NULL, 0}};
    assert(lr_file_pwritev(f, 0, valid, 3) == 0);
    memset(out, 0xcc, sizeof(out));
    assert(lr_file_preadv(f, 56, v, 2) == 0);
    assert(!memcmp(out, bytes, 8));
    assert(!memcmp(out + 16, (uint8_t[8]){0}, 8));
    assert(out[8] == 0xcc);
    lr_file_close(f);
    puts("file scalar/vector ranges: preflight preserves mapped/native destinations and native files: PASS");
}
