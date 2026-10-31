#ifndef QEMU_CUTILS_H
#define QEMU_CUTILS_H

/*
 * iec_binary_prefix:
 * @exp2: exponent of 2, a multiple of 10 between 0 and 60 inclusive.
 *
 * Return an IEC binary prefix (Ki, Mi, etc.) corresponding
 * to the given exponent of 2.
 */
const char *iec_binary_prefix(unsigned int exp2);

/**
 * pstrcpy:
 * @buf: buffer to copy string into
 * @buf_size: size of @buf in bytes
 * @str: string to copy
 *
 * Copy @str into @buf, including the trailing NUL, but do not
 * write more than @buf_size bytes. The resulting buffer is
 * always NUL terminated (even if the source string was too long).
 * If @buf_size is zero or negative then no bytes are copied.
 *
 * This function is similar to strncpy(), but avoids two of that
 * function's problems:
 *  * if @str fits in the buffer, pstrcpy() does not zero-fill the
 *    remaining space at the end of @buf
 *  * if @str is too long, pstrcpy() will copy the first @buf_size-1
 *    bytes and then add a NUL
 */
void pstrcpy(char *buf, int buf_size, const char *str);
/**
 * strpadcpy:
 * @buf: buffer to copy string into
 * @buf_size: size of @buf in bytes
 * @str: string to copy
 * @pad: character to pad the remainder of @buf with
 *
 * Copy @str into @buf (but *not* its trailing NUL!), and then pad the
 * rest of the buffer with the @pad character. If @str is too large
 * for the buffer then it is truncated, so that @buf contains the
 * first @buf_size characters of @str, with no terminator.
 */
void strpadcpy(char *buf, int buf_size, const char *str, char pad);
/**
 * pstrcat:
 * @buf: buffer containing existing string
 * @buf_size: size of @buf in bytes
 * @s: string to concatenate to @buf
 *
 * Append a copy of @s to the string already in @buf, but do not
 * allow the buffer to overflow. If the existing contents of @buf
 * plus @str would total more than @buf_size bytes, then write
 * as much of @str as will fit followed by a NUL terminator.
 *
 * @buf must already contain a NUL-terminated string, or the
 * behaviour is undefined.
 *
 * Returns: @buf.
 */
char *pstrcat(char *buf, int buf_size, const char *s);
/**
 * strstart:
 * @str: string to test
 * @val: prefix string to look for
 * @ptr: NULL, or pointer to be written to indicate start of
 *       the remainder of the string
 *
 * Test whether @str starts with the prefix @val.
 * If it does (including the degenerate case where @str and @val
 * are equal) then return true. If @ptr is not NULL then a
 * pointer to the first character following the prefix is written
 * to it. If @val is not a prefix of @str then return false (and
 * @ptr is not written to).
 *
 * Returns: true if @str starts with prefix @val, false otherwise.
 */
int strstart(const char *str, const char *val, const char **ptr);
/**
 * qemu_strnlen:
 * @s: string
 * @max_len: maximum number of bytes in @s to scan
 *
 * Return the length of the string @s, like strlen(), but do not
 * examine more than @max_len bytes of the memory pointed to by @s.
 * If no NUL terminator is found within @max_len bytes, then return
 * @max_len instead.
 *
 * This function has the same behaviour as the POSIX strnlen()
 * function.
 *
 * Returns: length of @s in bytes, or @max_len, whichever is smaller.
 */
int qemu_strnlen(const char *s, int max_len);
#ifdef HAVE_STRCHRNUL
static inline const char *qemu_strchrnul(const char *s, int c)
{
    return strchrnul(s, c);
}
#else
const char *qemu_strchrnul(const char *s, int c);
#endif
time_t mktimegm(struct tm *tm);
int qemu_parse_fd(const char *param);
int qemu_strtoi(const char *nptr, const char **endptr, int base,
                int *result);
int qemu_strtoui(const char *nptr, const char **endptr, int base,
                 unsigned int *result);
int qemu_strtol(const char *nptr, const char **endptr, int base,
                long *result);
int qemu_strtoul(const char *nptr, const char **endptr, int base,
                 unsigned long *result);
int qemu_strtoi64(const char *nptr, const char **endptr, int base,
                  int64_t *result);
int qemu_strtou64(const char *nptr, const char **endptr, int base,
                  uint64_t *result);
int qemu_strtod(const char *nptr, const char **endptr, double *result);
int qemu_strtod_finite(const char *nptr, const char **endptr, double *result);

int parse_uint(const char *s, const char **endptr, int base, uint64_t *value);
int parse_uint_full(const char *s, int base, uint64_t *value);

int qemu_strtosz(const char *nptr, const char **end, uint64_t *result);
int qemu_strtosz_MiB(const char *nptr, const char **end, uint64_t *result);
int qemu_strtosz_metric(const char *nptr, const char **end, uint64_t *result);

char *size_to_str(uint64_t val);

/* used to print char* safely */
#define STR_OR_NULL(str) ((str) ? (str) : "null")

/*
 * Check if a buffer is all zeroes.
 */

bool buffer_is_zero_ool(const void *vbuf, size_t len);
bool buffer_is_zero_ge256(const void *vbuf, size_t len);

static inline bool buffer_is_zero_sample3(const char *buf, size_t len)
{
    /*
     * For any reasonably sized buffer, these three samples come from
     * three different cachelines.  In qemu-img usage, we find that
     * each byte eliminates more than half of all buffer testing.
     * It is therefore critical to performance that the byte tests
     * short-circuit, so that we do not pull in additional cache lines.
     * Do not "optimize" this to !(a | b | c).
     */
    return !buf[0] && !buf[len - 1] && !buf[len / 2];
}

#ifdef __OPTIMIZE__
static inline bool buffer_is_zero(const void *buf, size_t len)
{
    return (__builtin_constant_p(len) && len >= 256
            ? buffer_is_zero_sample3(buf, len) &&
              buffer_is_zero_ge256(buf, len)
            : buffer_is_zero_ool(buf, len));
}
#else
#define buffer_is_zero  buffer_is_zero_ool
#endif

/*
 * Implementation of ULEB128 (http://en.wikipedia.org/wiki/LEB128)
 * Input is limited to 14-bit numbers
 */


/**
 * qemu_pstrcmp0:
 * @str1: a non-NULL pointer to a C string (*str1 can be NULL)
 * @str2: a non-NULL pointer to a C string (*str2 can be NULL)
 *
 * Compares *str1 and *str2 with g_strcmp0().
 *
 * Returns: an integer less than, equal to, or greater than zero, if
 * *str1 is <, == or > than *str2.
 */
int qemu_pstrcmp0(const char **str1, const char **str2);

/* Find program directory, and save it for later usage with
 * get_relocated_path().
 * Try OS specific API first, if not working, parse from argv0. */
void qemu_init_exec_dir(const char *argv0);

/**
 * get_relocated_path:
 * @dir: the directory (typically a `CONFIG_*DIR` variable) to be relocated.
 *
 * Returns a path for @dir that uses the directory of the running executable
 * as the prefix.
 *
 * When a directory named `qemu-bundle` exists in the directory of the running
 * executable, the path to the directory will be prepended to @dir. For
 * example, if the directory of the running executable is `/qemu/build` @dir
 * is `/usr/share/qemu`, the result will be
 * `/qemu/build/qemu-bundle/usr/share/qemu`. The directory is expected to exist
 * in the build tree.
 *
 * Otherwise, the directory of the running executable will be used as the
 * prefix and it appends the relative path from `bindir` to @dir. For example,
 * if the directory of the running executable is `/opt/qemu/bin`, `bindir` is
 * `/usr/bin` and @dir is `/usr/share/qemu`, the result will be
 * `/opt/qemu/bin/../share/qemu`.
 *
 * The returned string should be freed by the caller.
 */
char *get_relocated_path(const char *dir);

static inline const char *yes_no(bool b)
{
     return b ? "yes" : "no";
}

/**
 * qemu_hexdump_line:
 * @str: GString into which to append
 * @buf: buffer to dump
 * @len: number of bytes to dump
 * @unit_len: add a space between every @unit_len bytes
 * @block_len: add an extra space between every @block_len bytes
 *
 * Append @len bytes of @buf as hexadecimal into @str.
 * Add spaces between every @unit_len and @block_len bytes.
 * If @str is NULL, allocate a new string and return it;
 * otherwise return @str.
 */
GString *qemu_hexdump_line(GString *str, const void *buf, size_t len,
                           size_t unit_len, size_t block_len);

#endif
