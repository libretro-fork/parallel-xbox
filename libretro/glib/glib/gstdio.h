/*
 * The in-tree GLib: the file functions of <glib/gstdio.h>. On POSIX they
 * are the C library's, as in GLib; on Windows they take UTF-8 paths and
 * convert them with libretro-common's encoding_utf (gfile.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LR_GLIB_GSTDIO_H
#define LR_GLIB_GSTDIO_H

#include <glib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>

typedef struct stat GStatBuf;
#define g_open open
#define g_creat creat
#define g_fopen fopen
#define g_freopen freopen
#define g_stat stat
#define g_lstat lstat
#define g_unlink unlink
#define g_remove remove
#define g_rename rename
#define g_mkdir mkdir
#define g_rmdir rmdir
#define g_chdir chdir
#define g_access access
#define g_chmod chmod
#define g_fsync fsync

#else

typedef struct _stat64 GStatBuf;
int g_open(const gchar *filename, int flags, ...);
int g_creat(const gchar *filename, int mode);
FILE *g_fopen(const gchar *filename, const gchar *mode);
FILE *g_freopen(const gchar *filename, const gchar *mode, FILE *stream);
int g_stat(const gchar *filename, GStatBuf *buf);
#define g_lstat g_stat
int g_unlink(const gchar *filename);
int g_remove(const gchar *filename);
int g_rename(const gchar *oldfilename, const gchar *newfilename);
int g_mkdir(const gchar *filename, int mode);
int g_rmdir(const gchar *filename);
int g_chdir(const gchar *path);
int g_access(const gchar *filename, int mode);
int g_chmod(const gchar *filename, int mode);
int g_fsync(gint fd);

#endif

static inline gboolean g_close(gint fd, GError **error)
{
#ifdef _WIN32
    if (_close(fd) < 0) {
#else
    if (close(fd) < 0) {
#endif
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "close: %s", strerror(errno));
        return FALSE;
    }
    return TRUE;
}

#endif
