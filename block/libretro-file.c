/*
 * A disk image file over the host's own calls (block/libretro-file.h).
 *
 * POSIX: pread/pwrite (preadv/pwritev for a request in several pieces,
 * one system call), ftruncate for a size that leaves a hole, and
 * fallocate's PUNCH_HOLE (Linux, Android) or F_PUNCHHOLE (macOS) to free a
 * range. Windows: ReadFile/WriteFile at an offset, the file marked sparse
 * (FSCTL_SET_SPARSE) when it is written, FSCTL_SET_ZERO_DATA to free a
 * range. Where the file system cannot free a range (FAT, exFAT), it is
 * written with zeroes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "libretro-file.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if LR_FILE_FILESTREAM
#include <streams/file_stream.h>
#endif

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) || (defined(__linux__) && \
    (!defined(__ANDROID__) || __ANDROID_API__ >= 24))
#define LR_FILE_HAVE_PREADV 1
#endif

/* what a range that cannot be freed is written with */
static const uint8_t lr_file_zeroes[64 * 1024];

struct LRFile {
#if LR_FILE_FILESTREAM
    RFILE *stream;
#endif
    const uint8_t *mapped;
    int64_t mapped_size;
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    bool writable;
};

#ifdef _WIN32

static int lr_file_errno(void)
{
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        return EACCES;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_FUNCTION:
        return ENOTSUP;
    default:
        return EIO;
    }
}

static LRFile *lr_file_open_native(const char *path, bool writable, bool create)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *wpath;
    HANDLE h;
    LRFile *f;

    if (wlen <= 0) {
        errno = EINVAL;
        return NULL;
    }
    wpath = malloc(wlen * sizeof(*wpath));
    if (!wpath) {
        errno = ENOMEM;
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    h = CreateFileW(wpath, GENERIC_READ | (writable ? GENERIC_WRITE : 0),
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, create ? OPEN_ALWAYS : OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) {
        errno = lr_file_errno();
        return NULL;
    }
    f = calloc(1, sizeof(*f));
    if (!f) {
        CloseHandle(h);
        errno = ENOMEM;
        return NULL;
    }
    f->h = h;
    f->writable = writable;
    if (writable) {
        /* holes stay holes: without the flag NTFS allocates (and zeroes)
         * all of a size it is given. Fails on FAT and exFAT, which have
         * no sparse files: the file is then as big on the disk as it is */
        DWORD got;
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &got, NULL);
    }
    return f;
}

void lr_file_close(LRFile *f)
{
    if (!f) {
        return;
    }
#if LR_FILE_FILESTREAM
    if (f->stream) {
        filestream_close(f->stream);
        free(f);
        return;
    }
#endif
    if (f->writable) {
        FlushFileBuffers(f->h);
    }
    CloseHandle(f->h);
    free(f);
}

int64_t lr_file_size(LRFile *f)
{
    if (f->mapped) {
        return f->mapped_size;
    }
    LARGE_INTEGER sz;

    if (!GetFileSizeEx(f->h, &sz)) {
        return -lr_file_errno();
    }
    return sz.QuadPart;
}

int lr_file_set_size(LRFile *f, int64_t size)
{
    if (f->mapped) {
        return -EPERM;
    }
    FILE_END_OF_FILE_INFO eof;

    eof.EndOfFile.QuadPart = size;
    if (!SetFileInformationByHandle(f->h, FileEndOfFileInfo, &eof,
                                    sizeof(eof))) {
        return -lr_file_errno();
    }
    return 0;
}

/* one piece; *done: what moved, short only at the end of the file */
static int lr_file_rw(LRFile *f, int64_t off, void *buf, size_t len,
                      bool write, size_t *done)
{
    uint8_t *p = buf;

    *done = 0;

    while (len) {
        OVERLAPPED ov;
        DWORD n = len > 0x40000000u ? 0x40000000u : (DWORD)len, got = 0;
        BOOL ok;

        memset(&ov, 0, sizeof(ov));
        ov.Offset = (DWORD)off;
        ov.OffsetHigh = (DWORD)((uint64_t)off >> 32);
        ok = write ? WriteFile(f->h, p, n, &got, &ov)
                   : ReadFile(f->h, p, n, &got, &ov);
        if (!ok) {
            if (!write && GetLastError() == ERROR_HANDLE_EOF) {
                return 0;
            }
            return -lr_file_errno();
        }
        if (!got) {
            return write ? -EIO : 0;
        }
        p += got;
        off += got;
        len -= got;
        *done += got;
    }
    return 0;
}

int lr_file_zero(LRFile *f, int64_t off, int64_t len)
{
    if (f->mapped) {
        return -EPERM;
    }
    FILE_ZERO_DATA_INFORMATION z;
    DWORD got;
    int64_t size;

    if (len <= 0) {
        return 0;
    }
    if (off < 0) {
        return -EINVAL;
    }
    if (len > INT64_MAX - off) {
        len = INT64_MAX - off;
    }
    if (!len) {
        return 0;
    }
    z.FileOffset.QuadPart = off;
    z.BeyondFinalZero.QuadPart = off + len;
    /* This operation cannot extend EOF, so only the write fallback needs
     * a size query to clip its range. */
    if (DeviceIoControl(f->h, FSCTL_SET_ZERO_DATA, &z, sizeof(z), NULL, 0,
                        &got, NULL)) {
        return 0;
    }
    size = lr_file_size(f);
    if (size < 0) {
        return (int)size;
    }
    if (off >= size) {
        return 0;
    }
    if (len > size - off) {
        len = size - off;
    }
    while (len) {
        size_t n = len > (int64_t)sizeof(lr_file_zeroes) ?
                   sizeof(lr_file_zeroes) : (size_t)len, done;
        int ret = lr_file_rw(f, off, (void *)lr_file_zeroes, n, true, &done);
        if (ret < 0) {
            return ret;
        }
        off += n;
        len -= n;
    }
    return 0;
}

int lr_file_sync(LRFile *f)
{
    if (f->mapped) {
        return 0;
    }
    return FlushFileBuffers(f->h) ? 0 : -lr_file_errno();
}

int64_t lr_file_allocated(LRFile *f)
{
    if (f->mapped) {
        return -1;
    }
    FILE_STANDARD_INFO info;

    if (!GetFileInformationByHandleEx(f->h, FileStandardInfo, &info,
                                      sizeof(info))) {
        return -1;
    }
    return info.AllocationSize.QuadPart;
}

#else /* POSIX */

static LRFile *lr_file_open_native(const char *path, bool writable, bool create)
{
    int flags = (writable ? O_RDWR : O_RDONLY) | (create ? O_CREAT : 0);
    LRFile *f;
    int fd;

#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    do {
        fd = open(path, flags, 0644);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return NULL;
    }
    f = calloc(1, sizeof(*f));
    if (!f) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    f->fd = fd;
    f->writable = writable;
    return f;
}

void lr_file_close(LRFile *f)
{
    if (!f) {
        return;
    }
#if LR_FILE_FILESTREAM
    if (f->stream) {
        filestream_close(f->stream);
        free(f);
        return;
    }
#endif
    if (f->writable) {
        lr_file_sync(f);
    }
    close(f->fd);
    free(f);
}

int64_t lr_file_size(LRFile *f)
{
    if (f->mapped) {
        return f->mapped_size;
    }
    struct stat st;

    if (fstat(f->fd, &st) < 0) {
        return -errno;
    }
    return st.st_size;
}

int lr_file_set_size(LRFile *f, int64_t size)
{
    if (f->mapped) {
        return -EPERM;
    }
    int ret;

    do {
        ret = ftruncate(f->fd, (off_t)size);
    } while (ret < 0 && errno == EINTR);
    return ret < 0 ? -errno : 0;
}

static int lr_file_rw(LRFile *f, int64_t off, void *buf, size_t len,
                      bool write, size_t *done)
{
    uint8_t *p = buf;

    *done = 0;

    while (len) {
        ssize_t n = write ? pwrite(f->fd, p, len, (off_t)off)
                          : pread(f->fd, p, len, (off_t)off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (n == 0) {
            return write ? -EIO : 0;
        }
        p += n;
        off += n;
        len -= (size_t)n;
        *done += (size_t)n;
    }
    return 0;
}

/* the edges of a range that is freed whole blocks at a time, and every
 * range where nothing can be freed */
static int lr_file_write_zeroes(LRFile *f, int64_t off, int64_t len)
{
    while (len > 0) {
        size_t n = len > (int64_t)sizeof(lr_file_zeroes) ?
                   sizeof(lr_file_zeroes) : (size_t)len, done;
        int ret = lr_file_rw(f, off, (void *)lr_file_zeroes, n, true, &done);
        if (ret < 0) {
            return ret;
        }
        off += n;
        len -= n;
    }
    return 0;
}

int lr_file_zero(LRFile *f, int64_t off, int64_t len)
{
    if (f->mapped) {
        return -EPERM;
    }
    int64_t size;

    if (len <= 0) {
        return 0;
    }
    if (off < 0) {
        return -EINVAL;
    }
    if (len > INT64_MAX - off) {
        len = INT64_MAX - off;
    }
    if (!len) {
        return 0;
    }
#if defined(__linux__) && defined(FALLOC_FL_PUNCH_HOLE)
    if (fallocate(f->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  (off_t)off, (off_t)len) == 0) {
        return 0;
    }
#endif
    /* KEEP_SIZE above handles EOF itself; other paths need a current size. */
    size = lr_file_size(f);
    if (size < 0) {
        return (int)size;
    }
    if (off >= size) {
        return 0;
    }
    if (len > size - off) {
        len = size - off;
    }
#if defined(__APPLE__) && defined(F_PUNCHHOLE)
    {
        /* whole blocks of the file system only */
        struct stat st;
        if (fstat(f->fd, &st) == 0 && st.st_blksize > 0) {
            int64_t bs = st.st_blksize;
            int64_t a = (off + bs - 1) / bs * bs, b = (off + len) / bs * bs;
            if (b > a) {
                fpunchhole_t ph = { 0, 0, a, b - a };
                if (fcntl(f->fd, F_PUNCHHOLE, &ph) == 0) {
                    int ret = lr_file_write_zeroes(f, off, a - off);
                    return ret < 0 ? ret :
                           lr_file_write_zeroes(f, b, off + len - b);
                }
            }
        }
    }
#endif
    return lr_file_write_zeroes(f, off, len);
}

int lr_file_sync(LRFile *f)
{
    if (f->mapped) {
        return 0;
    }
    int ret;

    do {
#if defined(__linux__)
        ret = fdatasync(f->fd);
#else
        ret = fsync(f->fd);
#endif
    } while (ret < 0 && errno == EINTR);
    return ret < 0 ? -errno : 0;
}

int64_t lr_file_allocated(LRFile *f)
{
    if (f->mapped) {
        return -1;
    }
    struct stat st;

    if (fstat(f->fd, &st) < 0) {
        return -1;
    }
    return (int64_t)st.st_blocks * 512;
}

#endif

LRFile *lr_file_open(const char *path, bool writable, bool create)
{
#if LR_FILE_FILESTREAM
    if (!writable && !create) {
        RFILE *stream = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
                                        RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
        if (stream) {
            int64_t size = 0;
            const uint8_t *mapped = filestream_get_mapped_ptr(stream, &size);
            if (mapped && size >= 0) {
                LRFile *f = calloc(1, sizeof(*f));
                if (!f) {
                    filestream_close(stream);
                    errno = ENOMEM;
                    return NULL;
                }
                f->stream = stream;
#ifdef _WIN32
                f->h = INVALID_HANDLE_VALUE;
#else
                f->fd = -1;
#endif
                f->mapped = mapped;
                f->mapped_size = size;
                return f;
            }
            filestream_close(stream);
        }
    }
#endif
    /* Mapping is optional (including a >address-space file on 32-bit hosts).
     * Keep native positional/vectored I/O and sparse writable HDD behavior. */
    return lr_file_open_native(path, writable, create);
}

const uint8_t *lr_file_view(LRFile *f, int64_t off, size_t len)
{
    if (!f->mapped || off < 0 || off > f->mapped_size ||
        len > (uint64_t)(f->mapped_size - off)) {
        return NULL;
    }
    return f->mapped + off;
}

int lr_file_pread(LRFile *f, int64_t off, void *buf, size_t len)
{
    if (off < 0 || len > (uint64_t)(INT64_MAX - off)) {
        return -EINVAL;
    }
    size_t done;
    int ret;
    if (f->mapped) {
        if (off < 0) {
            return -EINVAL;
        }
        done = off >= f->mapped_size ? 0 : (size_t)(f->mapped_size - off);
        if (done > len) {
            done = len;
        }
        if (done) {
            memcpy(buf, f->mapped + off, done);
        }
        ret = 0;
    } else {
        ret = lr_file_rw(f, off, buf, len, false, &done);
    }

    if (ret == 0 && done < len) {
        memset((uint8_t *)buf + done, 0, len - done);
    }
    return ret;
}

int lr_file_pwrite(LRFile *f, int64_t off, const void *buf, size_t len)
{
    size_t done;
    if (f->mapped) {
        return -EPERM;
    }
    if (off < 0 || len > (uint64_t)(INT64_MAX - off)) {
        return -EINVAL;
    }

    return lr_file_rw(f, off, (void *)buf, len, true, &done);
}

/* EOF supplies zeros for the remaining destinations, without more host reads.
 * skip is within the first span; empty NULL spans need no pointer arithmetic. */
static int lr_file_zero_tail(const LRFileIOV *iov, int n, int64_t off, size_t skip)
{
    for (int i = 0; i < n; i++) {
        if (off < 0 || iov[i].len > (uint64_t)(INT64_MAX - off)) {
            return -EINVAL;
        }
        if (iov[i].len > skip) {
            memset((uint8_t *)iov[i].base + skip, 0, iov[i].len - skip);
        }
        off += iov[i].len;
        skip = 0;
    }
    return 0;
}

/* Validate the complete request before changing any destination or file. */
static int64_t lr_file_vector_length(int64_t off, const LRFileIOV *iov, int n)
{
    int64_t total = 0;

    if (off < 0 || n < 0 || (n && !iov)) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (iov[i].len > (uint64_t)(INT64_MAX - off - total)) {
            return -1;
        }
        total += (int64_t)iov[i].len;
    }
    return total;
}

int lr_file_preadv(LRFile *f, int64_t off, const LRFileIOV *iov, int n)
{
    int64_t total = lr_file_vector_length(off, iov, n);
    if (total < 0) {
        return -EINVAL;
    }
    if (!total) {
        return 0;
    }
    if (f->mapped) {
        for (int i = 0; i < n;) {
            void *base = iov[i].base;
            size_t length = iov[i++].len;
            while (i < n && length <= UINTPTR_MAX - (uintptr_t)base &&
                   (uintptr_t)iov[i].base == (uintptr_t)base + length &&
                   iov[i].len <= SIZE_MAX - length) {
                length += iov[i++].len;
            }
            if (off < 0 || length > (uint64_t)(INT64_MAX - off)) {
                return -EINVAL;
            }
            int ret = lr_file_pread(f, off, base, length);
            if (ret < 0) {
                return ret;
            }
            off += length;
        }
        return 0;
    }
#ifdef LR_FILE_HAVE_PREADV
    /* the whole request in one call (LRFileIOV is laid out as iovec), on
     * to the piece by piece path for what a short read left */
    if (n > 1 && n <= 1024 && sizeof(LRFileIOV) == sizeof(struct iovec)) {
        ssize_t got;
        do {
            got = preadv(f->fd, (const struct iovec *)iov, n, (off_t)off);
        } while (got < 0 && errno == EINTR);
        if (got < 0) {
            return -errno;
        }
        if ((uint64_t)got == (uint64_t)total) {
            return 0;
        }
        if (!got) {
            return lr_file_zero_tail(iov, n, off, 0);
        }
        while (n && (size_t)got >= iov->len) {
            got -= iov->len;
            off += iov->len;
            iov++;
            n--;
        }
        if (n) {
            size_t done;
            int ret = lr_file_rw(f, off + got, (uint8_t *)iov->base + got,
                                iov->len - got, false, &done);
            if (ret < 0) {
                return ret;
            }
            if (done < iov->len - got) {
                return lr_file_zero_tail(iov, n, off, (size_t)got + done);
            }
            off += iov->len;
            iov++;
            n--;
        }
    }
#endif
    for (int i = 0; i < n;) {
        void *base = iov[i].base;
        size_t length = iov[i++].len;
        while (i < n && length <= UINTPTR_MAX - (uintptr_t)base &&
               (uintptr_t)iov[i].base == (uintptr_t)base + length &&
               iov[i].len <= SIZE_MAX - length) {
            length += iov[i++].len;
        }
        if (off < 0 || length > (uint64_t)(INT64_MAX - off)) {
            return -EINVAL;
        }
        size_t done;
        int ret = lr_file_rw(f, off, base, length, false, &done);
        if (ret < 0) {
            return ret;
        }
        if (done < length) {
            memset((uint8_t *)base + done, 0, length - done);
            return lr_file_zero_tail(iov + i, n - i, off + length, 0);
        }
        off += length;
    }
    return 0;
}

int lr_file_pwritev(LRFile *f, int64_t off, const LRFileIOV *iov, int n)
{
    if (f->mapped) {
        return -EPERM;
    }
    int64_t total = lr_file_vector_length(off, iov, n);
    if (total < 0) {
        return -EINVAL;
    }
    if (!total) {
        return 0;
    }
#ifdef LR_FILE_HAVE_PREADV
    if (n > 1 && n <= 1024 && sizeof(LRFileIOV) == sizeof(struct iovec)) {
        ssize_t put;
        do {
            put = pwritev(f->fd, (const struct iovec *)iov, n, (off_t)off);
        } while (put < 0 && errno == EINTR);
        if (put < 0) {
            return -errno;
        }
        if ((uint64_t)put == (uint64_t)total) {
            return 0;
        }
        while (n && (size_t)put >= iov->len) {
            put -= iov->len;
            off += iov->len;
            iov++;
            n--;
        }
        if (n) {
            int ret = lr_file_pwrite(f, off + put,
                                     (const uint8_t *)iov->base + put,
                                     iov->len - put);
            if (ret < 0) {
                return ret;
            }
            off += iov->len;
            iov++;
            n--;
        }
    }
#endif
    for (int i = 0; i < n;) {
        const void *base = iov[i].base;
        size_t length = iov[i++].len;
        while (i < n && length <= UINTPTR_MAX - (uintptr_t)base &&
               (uintptr_t)iov[i].base == (uintptr_t)base + length &&
               iov[i].len <= SIZE_MAX - length) {
            length += iov[i++].len;
        }
        if (off < 0 || length > (uint64_t)(INT64_MAX - off)) {
            return -EINVAL;
        }
        int ret = lr_file_pwrite(f, off, base, length);
        if (ret < 0) {
            return ret;
        }
        off += length;
    }
    return 0;
}
