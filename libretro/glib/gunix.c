/*
 * The in-tree GLib: <glib-unix.h> (pipes and non-blocking descriptors) and,
 * on Windows, <glib/gstdio.h>'s UTF-8 file functions on libretro-common's
 * encoding_utf, and g_win32_error_message.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "glib.h"
#include "glib-unix.h"
#include "glib/gstdio.h"

#include <errno.h>
#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>

static gboolean g_unix_fail(GError **error, const char *what)
{
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s: %s", what,
                strerror(errno));
    return FALSE;
}

/* flags: FD_CLOEXEC or O_CLOEXEC, and O_NONBLOCK, for both ends */
gboolean g_unix_open_pipe(gint *fds, gint flags, GError **error)
{
    if (pipe(fds) < 0) {
        return g_unix_fail(error, "pipe");
    }
    for (int i = 0; i < 2; i++) {
        if ((flags & (FD_CLOEXEC | O_CLOEXEC)) &&
            fcntl(fds[i], F_SETFD, FD_CLOEXEC) < 0) {
            goto fail;
        }
        if ((flags & O_NONBLOCK) &&
            !g_unix_set_fd_nonblocking(fds[i], TRUE, NULL)) {
            goto fail;
        }
    }
    return TRUE;

fail:
    g_unix_fail(error, "fcntl");
    close(fds[0]);
    close(fds[1]);
    return FALSE;
}

gboolean g_unix_set_fd_nonblocking(gint fd, gboolean nonblock,
                                   GError **error)
{
    int f = fcntl(fd, F_GETFL);

    if (f < 0) {
        return g_unix_fail(error, "fcntl");
    }
    f = nonblock ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK);
    if (fcntl(fd, F_SETFL, f) < 0) {
        return g_unix_fail(error, "fcntl");
    }
    return TRUE;
}

#else

#include <direct.h>
#include <io.h>
#include <windows.h>

#include <encodings/utf.h>

#define G_WIDE(var, path)                                                  \
    wchar_t *var = utf8_to_utf16_string_alloc(path);                       \
    if (!var) {                                                            \
        errno = EINVAL;                                                    \
        return -1;                                                         \
    }

int g_open(const gchar *filename, int flags, ...)
{
    int mode = 0, r;
    va_list ap;

    va_start(ap, flags);
    if (flags & O_CREAT) {
        mode = va_arg(ap, int);
    }
    va_end(ap);
    {
        G_WIDE(w, filename);
        r = _wopen(w, flags | _O_BINARY * !(flags & _O_TEXT), mode);
        free(w);
    }
    return r;
}

int g_creat(const gchar *filename, int mode)
{
    return g_open(filename, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

FILE *g_fopen(const gchar *filename, const gchar *mode)
{
    wchar_t *w = utf8_to_utf16_string_alloc(filename);
    wchar_t *m = utf8_to_utf16_string_alloc(mode);
    FILE *f = w && m ? _wfopen(w, m) : NULL;

    free(w);
    free(m);
    return f;
}

FILE *g_freopen(const gchar *filename, const gchar *mode, FILE *stream)
{
    wchar_t *w = utf8_to_utf16_string_alloc(filename);
    wchar_t *m = utf8_to_utf16_string_alloc(mode);
    FILE *f = w && m ? _wfreopen(w, m, stream) : NULL;

    free(w);
    free(m);
    return f;
}

int g_stat(const gchar *filename, GStatBuf *buf)
{
    int r;
    G_WIDE(w, filename);

    r = _wstat64(w, buf);
    free(w);
    return r;
}

int g_unlink(const gchar *filename)
{
    int r;
    G_WIDE(w, filename);

    r = _wunlink(w);
    free(w);
    return r;
}

int g_remove(const gchar *filename)
{
    int r;
    G_WIDE(w, filename);

    r = _wremove(w);
    if (r < 0) {
        r = _wrmdir(w);
    }
    free(w);
    return r;
}

int g_rename(const gchar *oldfilename, const gchar *newfilename)
{
    wchar_t *o = utf8_to_utf16_string_alloc(oldfilename);
    wchar_t *n = utf8_to_utf16_string_alloc(newfilename);
    int r = o && n && MoveFileExW(o, n, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;

    if (r < 0) {
        errno = EACCES;
    }
    free(o);
    free(n);
    return r;
}

int g_mkdir(const gchar *filename, int mode)
{
    int r;
    G_WIDE(w, filename);

    r = _wmkdir(w);
    free(w);
    return r;
}

int g_rmdir(const gchar *filename)
{
    int r;
    G_WIDE(w, filename);

    r = _wrmdir(w);
    free(w);
    return r;
}

int g_chdir(const gchar *path)
{
    int r;
    G_WIDE(w, path);

    r = _wchdir(w);
    free(w);
    return r;
}

int g_access(const gchar *filename, int mode)
{
    int r;
    G_WIDE(w, filename);

    r = _waccess(w, mode & 06);
    free(w);
    return r;
}

int g_chmod(const gchar *filename, int mode)
{
    int r;
    G_WIDE(w, filename);

    r = _wchmod(w, mode);
    free(w);
    return r;
}

int g_fsync(gint fd)
{
    return _commit(fd);
}

/* the system's message for a Windows error code, in UTF-8 */
gchar *g_win32_error_message(gint error)
{
    wchar_t *msg = NULL;
    gchar *out = NULL;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                             FORMAT_MESSAGE_IGNORE_INSERTS |
                             FORMAT_MESSAGE_FROM_SYSTEM,
                             NULL, (DWORD)error, 0, (LPWSTR)&msg, 0, NULL);

    if (n && msg) {
        /* without the trailing line break */
        while (n && (msg[n - 1] == L'\n' || msg[n - 1] == L'\r')) {
            msg[--n] = 0;
        }
        {
            char *u = utf16_to_utf8_string_alloc(msg);
            out = g_strdup(u ? u : "");
            free(u);
        }
    }
    if (msg) {
        LocalFree(msg);
    }
    return out ? out : g_strdup_printf("Error %d", error);
}

#endif
