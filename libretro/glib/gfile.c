/*
 * The in-tree GLib (libretro/glib/glib.h): files, paths, directories and
 * mapped files.
 *
 * On libretro-common: files are read and written through its file stream
 * (and so its VFS), paths are taken apart and joined by file_path, and
 * directories are listed by retro_dirent. A mapped file is read into
 * memory: what QEMU maps is firmware read once at start, and GLib's
 * writable mappings are private copies anyway.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "glib.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <compat/strl.h>
#include <file/file_path.h>
#include <retro_dirent.h>
#include <retro_miscellaneous.h>
#include <streams/file_stream.h>

#ifdef _WIN32
#define G_IS_DIR_SEPARATOR_C(c) ((c) == '/' || (c) == '\\')
#else
#define G_IS_DIR_SEPARATOR_C(c) ((c) == '/')
#endif

static GFileError g_file_error_from_errno(gint err)
{
    switch (err) {
    case EEXIST:
        return G_FILE_ERROR_EXIST;
    case EISDIR:
        return G_FILE_ERROR_ISDIR;
    case EACCES:
        return G_FILE_ERROR_ACCES;
    case ENAMETOOLONG:
        return G_FILE_ERROR_NAMETOOLONG;
    case ENOENT:
        return G_FILE_ERROR_NOENT;
    case ENOTDIR:
        return G_FILE_ERROR_NOTDIR;
    default:
        return G_FILE_ERROR_FAILED;
    }
}

/* ---- files ---------------------------------------------------------------- */

gboolean g_file_test(const gchar *filename, GFileTest test)
{
    gboolean is_dir;

    if (!path_is_valid(filename)) {
        return FALSE;
    }
    if (test & G_FILE_TEST_EXISTS) {
        return TRUE;
    }
    is_dir = path_is_directory(filename);
    if ((test & G_FILE_TEST_IS_DIR) && is_dir) {
        return TRUE;
    }
    if ((test & G_FILE_TEST_IS_REGULAR) && !is_dir) {
        return TRUE;
    }
#ifndef _WIN32
    if ((test & G_FILE_TEST_IS_EXECUTABLE) && !is_dir &&
        access(filename, X_OK) == 0) {
        return TRUE;
    }
    if (test & G_FILE_TEST_IS_SYMLINK) {
        struct stat st;
        if (lstat(filename, &st) == 0 && S_ISLNK(st.st_mode)) {
            return TRUE;
        }
    }
#endif
    return FALSE;
}

gboolean g_file_get_contents(const gchar *filename, gchar **contents,
                             gsize *length, GError **error)
{
    void *buf = NULL;
    int64_t len = 0;

    if (!filestream_read_file(filename, &buf, &len) || !buf) {
        g_set_error(error, G_FILE_ERROR, path_is_valid(filename) ?
                    G_FILE_ERROR_FAILED : G_FILE_ERROR_NOENT,
                    "Failed to read \"%s\"", filename);
        *contents = NULL;
        return FALSE;
    }
    /* NUL-terminated and from malloc, as g_free takes it */
    *contents = buf;
    if (length) {
        *length = (gsize)len;
    }
    return TRUE;
}

gboolean g_file_set_contents(const gchar *filename, const gchar *contents,
                             gssize length, GError **error)
{
    if (length < 0) {
        length = (gssize)strlen(contents);
    }
    if (!filestream_write_file_atomic(filename, contents, length)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Failed to write \"%s\"", filename);
        return FALSE;
    }
    return TRUE;
}

gchar *g_file_read_link(const gchar *filename, GError **error)
{
#ifdef _WIN32
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "Failed to read the link \"%s\"", filename);
    return NULL;
#else
    gchar buf[PATH_MAX_LENGTH];
    ssize_t n = readlink(filename, buf, sizeof(buf) - 1);

    if (n < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Failed to read the link \"%s\": %s", filename,
                    strerror(errno));
        return NULL;
    }
    return g_strndup(buf, (gsize)n);
#endif
}

gint g_mkdir_with_parents(const gchar *pathname, gint mode)
{
    if (!pathname || !*pathname) {
        errno = EINVAL;
        return -1;
    }
    return path_mkdir(pathname) ? 0 : -1;
}

gchar *g_dir_make_tmp(const gchar *tmpl, GError **error)
{
    gchar *name, *path, *x;
    guint32 r = g_random_int();

    name = g_strdup(tmpl ? tmpl : ".XXXXXX");
    x = strstr(name, "XXXXXX");
    if (!x) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Template \"%s\" has no XXXXXX", name);
        g_free(name);
        return NULL;
    }
    for (int tries = 0; tries < 100; tries++, r = g_random_int()) {
        for (int i = 0; i < 6; i++) {
            x[i] = "abcdefghijklmnopqrstuvwxyz0123456789"[(r >> (i * 5)) % 36];
        }
        path = g_build_filename(g_get_tmp_dir(), name, NULL);
        if (!path_is_valid(path) && path_mkdir(path)) {
            g_free(name);
            return path;
        }
        g_free(path);
    }
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "Failed to make a directory from \"%s\"", tmpl);
    g_free(name);
    return NULL;
}

/* a new file in the temporary directory, opened for reading and writing */
gint g_file_open_tmp(const gchar *tmpl, gchar **name_used, GError **error)
{
    gchar *name, *path, *x;
    guint32 r = g_random_int();
    gint fd;

    name = g_strdup(tmpl ? tmpl : ".XXXXXX");
    x = strstr(name, "XXXXXX");
    if (!x || strchr(name, '/') || strchr(name, G_DIR_SEPARATOR)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Template \"%s\" is invalid", name);
        g_free(name);
        return -1;
    }
    for (int tries = 0; tries < 100; tries++, r = g_random_int()) {
        for (int i = 0; i < 6; i++) {
            x[i] = "abcdefghijklmnopqrstuvwxyz0123456789"[(r >> (i * 5)) % 36];
        }
        path = g_build_filename(g_get_tmp_dir(), name, NULL);
#ifdef _WIN32
        fd = _open(path, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, 0600);
#else
        fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
        if (fd >= 0) {
            g_free(name);
            if (name_used) {
                *name_used = path;
            } else {
                g_free(path);
            }
            return fd;
        }
        g_free(path);
        if (errno != EEXIST) {
            break;
        }
    }
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "Failed to make a file from \"%s\"", name);
    g_free(name);
    return -1;
}

/* ---- paths ---------------------------------------------------------------- */

gboolean g_path_is_absolute(const gchar *file_name)
{
    return path_is_absolute(file_name);
}

/* the last element, without trailing separators; "." for an empty path */
gchar *g_path_get_basename(const gchar *file_name)
{
    gchar *s, *base;
    gsize len;

    if (!*file_name) {
        return g_strdup(".");
    }
    s = g_strdup(file_name);
    len = strlen(s);
    while (len > 1 && G_IS_DIR_SEPARATOR_C(s[len - 1])) {
        s[--len] = 0;
    }
    base = g_strdup(path_basename(s));
    g_free(s);
    return *base ? base : (g_free(base), g_strdup(G_DIR_SEPARATOR_S));
}

/* everything before the last element; "." when there is none */
gchar *g_path_get_dirname(const gchar *file_name)
{
    gchar buf[PATH_MAX_LENGTH];
    gsize len;

    fill_pathname_basedir(buf, file_name, sizeof(buf));
    len = strlen(buf);
    while (len > 1 && G_IS_DIR_SEPARATOR_C(buf[len - 1])) {
        buf[--len] = 0;
    }
    return g_strdup(len ? buf : ".");
}

static gchar *g_build_va(const gchar *sep, const gchar *first, va_list ap)
{
    GString *out = g_string_new(NULL);
    gsize seplen = strlen(sep);

    for (const gchar *e = first; e; e = va_arg(ap, const gchar *)) {
        const gchar *start = e, *end = e + strlen(e);

        if (!*e) {
            continue;
        }
        /* separators between elements are collapsed to one */
        if (out->len) {
            while (start < end && !strncmp(start, sep, seplen)) {
                start += seplen;
            }
            while (out->len >= seplen &&
                   !strcmp(out->str + out->len - seplen, sep)) {
                g_string_truncate(out, out->len - seplen);
            }
            g_string_append(out, sep);
        }
        g_string_append_len(out, start, end - start);
    }
    return g_string_free(out, FALSE);
}

gchar *g_build_path(const gchar *separator, const gchar *first_element, ...)
{
    gchar *s;
    va_list ap;

    va_start(ap, first_element);
    s = g_build_va(separator, first_element, ap);
    va_end(ap);
    return s;
}

/*
 * As g_build_path, with either separator taken on Windows; elements are
 * joined by the first separator any of them uses, else the system's.
 */
gchar *g_build_filename(const gchar *first_element, ...)
{
    GString *out = g_string_new(NULL);
    gchar sep[2] = { G_DIR_SEPARATOR, 0 };
    const gchar *e;
    va_list ap;

    va_start(ap, first_element);
    for (e = first_element; e; e = va_arg(ap, const gchar *)) {
        const gchar *c = e;
        while (*c && !G_IS_DIR_SEPARATOR_C(*c)) {
            c++;
        }
        if (*c) {
            sep[0] = *c;
            break;
        }
    }
    va_end(ap);

    va_start(ap, first_element);
    for (e = first_element; e; e = va_arg(ap, const gchar *)) {
        const gchar *start = e;

        if (!*e) {
            continue;
        }
        if (out->len) {
            while (*start && G_IS_DIR_SEPARATOR_C(*start)) {
                start++;
            }
            while (out->len > 1 &&
                   G_IS_DIR_SEPARATOR_C(out->str[out->len - 1])) {
                g_string_truncate(out, out->len - 1);
            }
            if (!G_IS_DIR_SEPARATOR_C(out->str[out->len - 1])) {
                g_string_append(out, sep);
            }
        }
        g_string_append(out, start);
    }
    va_end(ap);
    return g_string_free(out, FALSE);
}

gchar *g_get_current_dir(void)
{
    gchar buf[PATH_MAX_LENGTH];

#ifdef _WIN32
    if (!_getcwd(buf, sizeof(buf))) {
#else
    if (!getcwd(buf, sizeof(buf))) {
#endif
        return g_strdup(G_DIR_SEPARATOR_S);
    }
    return g_strdup(buf);
}

/* the system's shared data directories: GLib's own defaults */
const gchar *const *g_get_system_data_dirs(void)
{
    static const gchar *dirs[2];

    if (!dirs[0]) {
#ifdef _WIN32
        const gchar *d = g_getenv("ProgramData");
        dirs[0] = d && *d ? d : g_get_tmp_dir();
#else
        dirs[0] = "/usr/share";
#endif
    }
    return dirs;
}

/* ".", ".." and repeated separators resolved, made absolute */
gchar *g_canonicalize_filename(const gchar *filename,
                               const gchar *relative_to)
{
    gchar buf[PATH_MAX_LENGTH];

    if (path_is_absolute(filename)) {
        strlcpy(buf, filename, sizeof(buf));
    } else {
        gchar *base = relative_to ? g_strdup(relative_to)
                                  : g_get_current_dir();
        fill_pathname_join(buf, base, filename, sizeof(buf));
        g_free(base);
    }
    if (!path_resolve_realpath(buf, sizeof(buf), false)) {
        return g_strdup(filename);
    }
    return g_strdup(buf);
}

/* the core starts no programs, and so never looks one up */
gchar *g_find_program_in_path(const gchar *program)
{
    return NULL;
}

/* ---- directories ---------------------------------------------------------- */

struct _GDir {
    struct RDIR *dir;
};

GDir *g_dir_open(const gchar *path, guint flags, GError **error)
{
    struct RDIR *d = retro_opendir_include_hidden(path, true);
    GDir *dir;

    if (!d) {
        g_set_error(error, G_FILE_ERROR, path_is_valid(path) ?
                    G_FILE_ERROR_NOTDIR : G_FILE_ERROR_NOENT,
                    "Error opening directory \"%s\"", path);
        return NULL;
    }
    dir = g_new(GDir, 1);
    dir->dir = d;
    return dir;
}

/* every name but "." and "..", as GLib gives them */
const gchar *g_dir_read_name(GDir *dir)
{
    while (retro_readdir(dir->dir)) {
        const char *name = retro_dirent_get_name(dir->dir);
        if (name && strcmp(name, ".") && strcmp(name, "..")) {
            return name;
        }
    }
    return NULL;
}

void g_dir_close(GDir *dir)
{
    if (dir) {
        retro_closedir(dir->dir);
        g_free(dir);
    }
}

/* ---- mapped files --------------------------------------------------------- */

struct _GMappedFile {
    gchar *contents;
    gsize length;
    gint ref_count;
};

static GMappedFile *g_mapped_file_take(gchar *contents, gsize length)
{
    GMappedFile *m = g_new(GMappedFile, 1);

    m->contents = contents;
    m->length = length;
    m->ref_count = 1;
    return m;
}

GMappedFile *g_mapped_file_new(const gchar *filename, gboolean writable,
                               GError **error)
{
    gchar *contents;
    gsize length;

    if (!g_file_get_contents(filename, &contents, &length, error)) {
        return NULL;
    }
    return g_mapped_file_take(contents, length);
}

/* the file the descriptor is open on, from its start, read in full */
GMappedFile *g_mapped_file_new_from_fd(gint fd, gboolean writable,
                                       GError **error)
{
    GString *s = g_string_new(NULL);
    gchar buf[65536];
    gsize length;
#ifdef _WIN32
    int n;
#else
    ssize_t n;
#endif

    if (lseek(fd, 0, SEEK_SET) < 0) {
        goto fail;
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        g_string_append_len(s, buf, n);
    }
    if (n < 0) {
        goto fail;
    }
    length = s->len;
    return g_mapped_file_take(g_string_free(s, FALSE), length);

fail:
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "Failed to read the file: %s", strerror(errno));
    g_string_free(s, TRUE);
    return NULL;
}

gsize g_mapped_file_get_length(GMappedFile *file)
{
    return file->length;
}

gchar *g_mapped_file_get_contents(GMappedFile *file)
{
    return file->contents;
}

GMappedFile *g_mapped_file_ref(GMappedFile *file)
{
    file->ref_count++;
    return file;
}

void g_mapped_file_unref(GMappedFile *file)
{
    if (file && --file->ref_count == 0) {
        g_free(file->contents);
        g_free(file);
    }
}
