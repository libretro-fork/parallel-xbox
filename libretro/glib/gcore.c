/*
 * The in-tree GLib (libretro/glib/glib.h): memory, messages, errors,
 * strings, GString, the system, time, random numbers and once.
 *
 * On libretro-common: strlcpy/strlcat (compat_strl), UTF-8 and UTF-16
 * (encoding_utf), base64 (encoding_base64), SHA-1 (lrc_hash), the clock and
 * the processor count (features_cpu), sleeping (retro_timers), atomics
 * (retro_atomic). What is left (GLib's own semantics on top of libc) is
 * written here. There is no GMutex: nothing in the core takes one.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "glib.h"

#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <compat/strl.h>
#include <encodings/base64.h>
#include <encodings/utf.h>
#include <features/features_cpu.h>
#include <lrc_hash.h>
#include <retro_atomic.h>
#include <retro_timers.h>

/* Protect process-wide compatibility state, including on hosts without GLib. */
static void g_compat_lock(retro_atomic_int_t *lock)
{
    while (!retro_atomic_cas_int(lock, 0, 1)) {
        retro_sleep(0);
    }
}

static void g_compat_unlock(retro_atomic_int_t *lock)
{
    retro_atomic_store_release_int(lock, 0);
}

/* ---- memory --------------------------------------------------------------- */

static G_NORETURN void g_oom(gsize n)
{
    fprintf(stderr, "GLib: failed to allocate %zu bytes\n", (size_t)n);
    abort();
}

gpointer g_malloc(gsize n)
{
    gpointer p;

    if (!n) {
        return NULL;
    }
    p = malloc(n);
    if (!p) {
        g_oom(n);
    }
    return p;
}

gpointer g_malloc0(gsize n)
{
    gpointer p;

    if (!n) {
        return NULL;
    }
    p = calloc(1, n);
    if (!p) {
        g_oom(n);
    }
    return p;
}

gpointer g_realloc(gpointer mem, gsize n)
{
    gpointer p;

    if (!n) {
        free(mem);
        return NULL;
    }
    p = realloc(mem, n);
    if (!p) {
        g_oom(n);
    }
    return p;
}

static gsize g_mul(gsize a, gsize b)
{
    if (b && a > G_MAXSIZE / b) {
        g_oom(G_MAXSIZE);
    }
    return a * b;
}

gpointer g_malloc_n(gsize n, gsize size)
{
    return g_malloc(g_mul(n, size));
}

gpointer g_malloc0_n(gsize n, gsize size)
{
    return g_malloc0(g_mul(n, size));
}

gpointer g_realloc_n(gpointer mem, gsize n, gsize size)
{
    return g_realloc(mem, g_mul(n, size));
}

gpointer g_try_malloc(gsize n)
{
    return n ? malloc(n) : NULL;
}

gpointer g_try_malloc0(gsize n)
{
    return n ? calloc(1, n) : NULL;
}

gpointer g_try_malloc_n(gsize n, gsize size)
{
    return (size && n > G_MAXSIZE / size) ? NULL : g_try_malloc(n * size);
}

gpointer g_try_malloc0_n(gsize n, gsize size)
{
    return (size && n > G_MAXSIZE / size) ? NULL : g_try_malloc0(n * size);
}

gpointer g_try_realloc(gpointer mem, gsize n)
{
    if (!n) {
        free(mem);
        return NULL;
    }
    return realloc(mem, n);
}

void g_free(gpointer mem)
{
    free(mem);
}

gpointer g_memdup2(gconstpointer mem, gsize byte_size)
{
    gpointer p;

    if (!mem || !byte_size) {
        return NULL;
    }
    p = g_malloc(byte_size);
    memcpy(p, mem, byte_size);
    return p;
}

gpointer g_memdup(gconstpointer mem, guint byte_size)
{
    return g_memdup2(mem, byte_size);
}

/* ---- messages and assertions ---------------------------------------------- */

static GLogFunc g_log_handler;
static gpointer g_log_handler_data;

GLogFunc g_log_set_default_handler(GLogFunc log_func, gpointer user_data)
{
    GLogFunc old = g_log_handler;

    g_log_handler = log_func;
    g_log_handler_data = user_data;
    return old;
}

void g_logv(const gchar *log_domain, GLogLevelFlags log_level,
            const gchar *format, va_list args)
{
    gchar *msg = g_strdup_vprintf(format, args);

    if (g_log_handler) {
        g_log_handler(log_domain, log_level, msg, g_log_handler_data);
    } else if (!(log_level & (G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO))) {
        fprintf(stderr, "%s%s%s\n", log_domain ? log_domain : "",
                log_domain ? ": " : "", msg);
    }
    g_free(msg);
}

void g_log(const gchar *log_domain, GLogLevelFlags log_level,
           const gchar *format, ...)
{
    va_list ap;

    va_start(ap, format);
    g_logv(log_domain, log_level, format, ap);
    va_end(ap);
}

void g_assertion_message_expr(const char *domain, const char *file, int line,
                              const char *func, const char *expr)
{
    if (expr) {
        fprintf(stderr, "%s:%d:%s: assertion failed: (%s)\n", file, line,
                func, expr);
    } else {
        fprintf(stderr, "%s:%d:%s: code should not be reached\n", file, line,
                func);
    }
    abort();
}

void g_return_if_fail_warning(const char *log_domain,
                              const char *pretty_function,
                              const char *expression)
{
    g_log(log_domain, G_LOG_LEVEL_CRITICAL, "%s: assertion '%s' failed",
          pretty_function, expression);
}

void g_warn_message(const char *domain, const char *file, int line,
                    const char *func, const char *warnexpr)
{
    if (warnexpr) {
        g_log(domain, G_LOG_LEVEL_WARNING, "%s:%d:%s: runtime check failed: "
              "(%s)", file, line, func, warnexpr);
    } else {
        g_log(domain, G_LOG_LEVEL_WARNING, "%s:%d:%s: code should not be "
              "reached", file, line, func);
    }
}

/* ---- quarks and errors ---------------------------------------------------- */

/* Interned strings live for the process lifetime. Published nodes are
 * immutable, so readers need neither a lock nor a reclamation protocol. */
typedef struct GQuarkEntry {
    struct GQuarkEntry *next;
    gchar *string;
    GQuark id;
} GQuarkEntry;
static retro_atomic_ptr_t g_quarks;

static GQuark g_quark_find(GQuarkEntry *entry, const gchar *string)
{
    if (!string) {
        return 0;
    }
    for (; entry; entry = entry->next) {
        if (!strcmp(entry->string, string)) {
            return entry->id;
        }
    }
    return 0;
}

GQuark g_quark_try_string(const gchar *string)
{
    return g_quark_find(retro_atomic_load_acquire_ptr(&g_quarks), string);
}

GQuark g_quark_from_string(const gchar *string)
{
    GQuarkEntry *candidate = NULL;

    if (!string) {
        return 0;
    }
    for (;;) {
        GQuarkEntry *head = retro_atomic_load_acquire_ptr(&g_quarks);
        GQuark q = g_quark_find(head, string);
        if (q) {
            if (candidate) {
                g_free(candidate->string);
                g_free(candidate);
            }
            return q;
        }
        if (!candidate) {
            candidate = g_new(GQuarkEntry, 1);
            candidate->string = g_strdup(string);
        }
        candidate->next = head;
        candidate->id = head ? head->id + 1 : 1;
        if (!candidate->id) {
            abort();
        }
        /* Save the result before publication; never modify the entry
         * once another thread can see it. A failed CAS rescans for a
         * concurrent insertion of the same string. */
        q = candidate->id;
        if (retro_atomic_cas_ptr(&g_quarks, head, candidate)) {
            return q;
        }
    }
}

GQuark g_quark_from_static_string(const gchar *string)
{
    return g_quark_from_string(string);
}

const gchar *g_quark_to_string(GQuark quark)
{
    GQuarkEntry *entry = retro_atomic_load_acquire_ptr(&g_quarks);

    for (; entry && entry->id >= quark; entry = entry->next) {
        if (entry->id == quark) {
            return entry->string;
        }
    }
    return NULL;
}

GError *g_error_new(GQuark domain, gint code, const gchar *format, ...)
{
    GError *e = g_new0(GError, 1);
    va_list ap;

    va_start(ap, format);
    e->message = g_strdup_vprintf(format, ap);
    va_end(ap);
    e->domain = domain;
    e->code = code;
    return e;
}

void g_error_free(GError *error)
{
    if (error) {
        g_free(error->message);
        g_free(error);
    }
}

void g_set_error(GError **err, GQuark domain, gint code,
                 const gchar *format, ...)
{
    GError *e;
    va_list ap;

    if (!err) {
        return;
    }
    e = g_new0(GError, 1);
    va_start(ap, format);
    e->message = g_strdup_vprintf(format, ap);
    va_end(ap);
    e->domain = domain;
    e->code = code;
    *err = e;
}

void g_clear_error(GError **err)
{
    if (err && *err) {
        g_error_free(*err);
        *err = NULL;
    }
}

/* ---- strings -------------------------------------------------------------- */

gchar *g_strdup(const gchar *str)
{
    return str ? g_memdup2(str, strlen(str) + 1) : NULL;
}

gchar *g_strndup(const gchar *str, gsize n)
{
    gchar *s;
    gsize len = 0;

    if (!str) {
        return NULL;
    }
    while (len < n && str[len]) {
        len++;
    }
    s = g_malloc(n + 1);
    memcpy(s, str, len);
    memset(s + len, 0, n + 1 - len);
    return s;
}

gint g_vasprintf(gchar **string, const gchar *format, va_list args)
{
    va_list ap;
    int n;

    va_copy(ap, args);
    n = vsnprintf(NULL, 0, format, ap);
    va_end(ap);
    if (n < 0) {
        *string = NULL;
        return -1;
    }
    *string = g_malloc((gsize)n + 1);
    vsnprintf(*string, (size_t)n + 1, format, args);
    return n;
}

gchar *g_strdup_vprintf(const gchar *format, va_list args)
{
    gchar *s;

    g_vasprintf(&s, format, args);
    return s;
}

gchar *g_strdup_printf(const gchar *format, ...)
{
    gchar *s;
    va_list ap;

    va_start(ap, format);
    g_vasprintf(&s, format, ap);
    va_end(ap);
    return s;
}

gint g_vsnprintf(gchar *string, gulong n, const gchar *format, va_list args)
{
    return vsnprintf(string, n, format, args);
}

gint g_snprintf(gchar *string, gulong n, const gchar *format, ...)
{
    va_list ap;
    int r;

    va_start(ap, format);
    r = vsnprintf(string, n, format, ap);
    va_end(ap);
    return r;
}

static gchar *g_concat_va(const gchar *sep, const gchar *first, va_list ap)
{
    gsize seplen = sep ? strlen(sep) : 0, len = 0;
    const gchar *s;
    gchar *out, *p;
    va_list ap2;

    if (!first) {
        return g_strdup("");
    }
    va_copy(ap2, ap);
    len = strlen(first);
    while ((s = va_arg(ap2, const gchar *))) {
        len += seplen + strlen(s);
    }
    va_end(ap2);
    out = p = g_malloc(len + 1);
    p = memcpy(p, first, strlen(first)) + strlen(first);
    while ((s = va_arg(ap, const gchar *))) {
        if (seplen) {
            memcpy(p, sep, seplen);
            p += seplen;
        }
        memcpy(p, s, strlen(s));
        p += strlen(s);
    }
    *p = 0;
    return out;
}

gchar *g_strconcat(const gchar *string1, ...)
{
    gchar *s;
    va_list ap;

    va_start(ap, string1);
    s = g_concat_va(NULL, string1, ap);
    va_end(ap);
    return s;
}

gchar *g_strjoin(const gchar *separator, ...)
{
    gchar *s;
    const gchar *first;
    va_list ap;

    va_start(ap, separator);
    first = va_arg(ap, const gchar *);
    s = g_concat_va(separator, first, ap);
    va_end(ap);
    return s;
}

gsize g_strlcpy(gchar *dest, const gchar *src, gsize dest_size)
{
    return strlcpy(dest, src, dest_size);
}

gsize g_strlcat(gchar *dest, const gchar *src, gsize dest_size)
{
    return strlcat(dest, src, dest_size);
}

gint g_strcmp0(const char *str1, const char *str2)
{
    if (!str1) {
        return -(str1 != str2);
    }
    if (!str2) {
        return 1;
    }
    return strcmp(str1, str2);
}

gboolean g_str_has_prefix(const gchar *str, const gchar *prefix)
{
    return !strncmp(str, prefix, strlen(prefix));
}

gboolean g_str_has_suffix(const gchar *str, const gchar *suffix)
{
    gsize l = strlen(str), s = strlen(suffix);

    return l >= s && !strcmp(str + l - s, suffix);
}

gchar *g_strstr_len(const gchar *haystack, gssize haystack_len,
                    const gchar *needle)
{
    gsize nlen = strlen(needle);

    if (haystack_len < 0) {
        return strstr(haystack, needle);
    }
    for (gssize i = 0; i + (gssize)nlen <= haystack_len && haystack[i]; i++) {
        if (!memcmp(haystack + i, needle, nlen)) {
            return (gchar *)haystack + i;
        }
    }
    return NULL;
}

gchar *g_strrstr(const gchar *haystack, const gchar *needle)
{
    gsize nlen = strlen(needle), hlen = strlen(haystack);

    for (gssize i = (gssize)hlen - (gssize)nlen; i >= 0; i--) {
        if (!memcmp(haystack + i, needle, nlen)) {
            return (gchar *)haystack + i;
        }
    }
    return NULL;
}

gchar **g_strsplit(const gchar *string, const gchar *delimiter,
                   gint max_tokens)
{
    GPtrArray *a = g_ptr_array_new();
    gsize dlen = strlen(delimiter);
    const gchar *s = string, *hit;

    if (max_tokens < 1) {
        max_tokens = G_MAXINT;
    }
    if (*string) {
        while (--max_tokens && (hit = strstr(s, delimiter))) {
            g_ptr_array_add(a, g_strndup(s, (gsize)(hit - s)));
            s = hit + dlen;
        }
        g_ptr_array_add(a, g_strdup(s));
    }
    g_ptr_array_add(a, NULL);
    return (gchar **)g_ptr_array_free(a, FALSE);
}

gchar *g_strjoinv(const gchar *separator, gchar **str_array)
{
    GString *s = g_string_new("");

    for (guint i = 0; str_array[i]; i++) {
        if (i && separator) {
            g_string_append(s, separator);
        }
        g_string_append(s, str_array[i]);
    }
    return g_string_free(s, FALSE);
}

void g_strfreev(gchar **str_array)
{
    if (str_array) {
        for (guint i = 0; str_array[i]; i++) {
            g_free(str_array[i]);
        }
        g_free(str_array);
    }
}

guint g_strv_length(gchar **str_array)
{
    guint n = 0;

    while (str_array && str_array[n]) {
        n++;
    }
    return n;
}

gchar **g_strdupv(gchar **str_array)
{
    guint n = g_strv_length(str_array);
    gchar **v;

    if (!str_array) {
        return NULL;
    }
    v = g_new(gchar *, n + 1);
    for (guint i = 0; i < n; i++) {
        v[i] = g_strdup(str_array[i]);
    }
    v[n] = NULL;
    return v;
}

gboolean g_strv_contains(const gchar *const *strv, const gchar *str)
{
    for (; *strv; strv++) {
        if (!strcmp(*strv, str)) {
            return TRUE;
        }
    }
    return FALSE;
}

gchar *g_strchug(gchar *string)
{
    gchar *s = string;

    while (*s && g_ascii_isspace(*s)) {
        s++;
    }
    memmove(string, s, strlen(s) + 1);
    return string;
}

gchar *g_strchomp(gchar *string)
{
    gsize len = strlen(string);

    while (len && g_ascii_isspace(string[len - 1])) {
        string[--len] = 0;
    }
    return string;
}

gchar *g_strstrip(gchar *string)
{
    return g_strchomp(g_strchug(string));
}

gchar *g_strcanon(gchar *string, const gchar *valid_chars, gchar substitutor)
{
    for (gchar *c = string; *c; c++) {
        if (!strchr(valid_chars, *c)) {
            *c = substitutor;
        }
    }
    return string;
}

gchar *g_strdelimit(gchar *string, const gchar *delimiters,
                    gchar new_delimiter)
{
    if (!delimiters) {
        delimiters = "_-|> <.";
    }
    for (gchar *c = string; *c; c++) {
        if (strchr(delimiters, *c)) {
            *c = new_delimiter;
        }
    }
    return string;
}

gchar *g_strreverse(gchar *string)
{
    gsize len = strlen(string);

    for (gsize i = 0; i < len / 2; i++) {
        gchar t = string[i];
        string[i] = string[len - 1 - i];
        string[len - 1 - i] = t;
    }
    return string;
}

gchar g_ascii_tolower(gchar c)
{
    return g_ascii_isupper(c) ? c - 'A' + 'a' : c;
}

gchar g_ascii_toupper(gchar c)
{
    return g_ascii_islower(c) ? c - 'a' + 'A' : c;
}

gchar *g_ascii_strdown(const gchar *str, gssize len)
{
    gchar *s = len < 0 ? g_strdup(str) : g_strndup(str, (gsize)len);

    for (gchar *c = s; *c; c++) {
        *c = g_ascii_tolower(*c);
    }
    return s;
}

gchar *g_ascii_strup(const gchar *str, gssize len)
{
    gchar *s = len < 0 ? g_strdup(str) : g_strndup(str, (gsize)len);

    for (gchar *c = s; *c; c++) {
        *c = g_ascii_toupper(*c);
    }
    return s;
}

gint g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n)
{
    for (; n; n--, s1++, s2++) {
        gint c1 = g_ascii_tolower(*s1), c2 = g_ascii_tolower(*s2);
        if (c1 != c2 || !c1) {
            return c1 - c2;
        }
    }
    return 0;
}

gint g_ascii_strcasecmp(const gchar *s1, const gchar *s2)
{
    return g_ascii_strncasecmp(s1, s2, G_MAXSIZE);
}

gint64 g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base)
{
    return strtoll(nptr, endptr, (int)base);
}

guint64 g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base)
{
    return strtoull(nptr, endptr, (int)base);
}

gdouble g_ascii_strtod(const gchar *nptr, gchar **endptr)
{
    return strtod(nptr, endptr);
}

gint g_ascii_digit_value(gchar c)
{
    return g_ascii_isdigit(c) ? c - '0' : -1;
}

gint g_ascii_xdigit_value(gchar c)
{
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return g_ascii_digit_value(c);
}

const gchar *g_strerror(gint errnum)
{
    return strerror(errnum);
}

const gchar *g_strsignal(gint signum)
{
    return "signal";
}

/* '*' any run, '?' any one character */
gboolean g_pattern_match_simple(const gchar *pattern, const gchar *string)
{
    if (!*pattern) {
        return !*string;
    }
    if (*pattern == '*') {
        while (*pattern == '*') {
            pattern++;
        }
        if (!*pattern) {
            return TRUE;
        }
        for (; *string; string++) {
            if (g_pattern_match_simple(pattern, string)) {
                return TRUE;
            }
        }
        return FALSE;
    }
    if (!*string || (*pattern != '?' && *pattern != *string)) {
        return FALSE;
    }
    return g_pattern_match_simple(pattern + 1, string + 1);
}

/* ---- UTF-8 ---------------------------------------------------------------- */

gboolean g_utf8_validate(const gchar *str, gssize max_len, const gchar **end)
{
    const guchar *s = (const guchar *)str;
    const guchar *e = max_len < 0 ? NULL : s + max_len;
    gboolean ok = TRUE;

    while ((e ? s < e : 1) && *s) {
        guint n = *s < 0x80 ? 0 : (*s & 0xe0) == 0xc0 ? 1 :
                  (*s & 0xf0) == 0xe0 ? 2 : (*s & 0xf8) == 0xf0 ? 3 : 4;
        if (n == 4 || (*s & 0xfe) == 0xc0 || *s > 0xf4) {
            ok = FALSE;
            break;
        }
        if (e && (gsize)(e - s) <= n) {
            ok = n == 0;
            if (!ok) {
                break;
            }
        }
        for (guint i = 1; i <= n; i++) {
            if ((s[i] & 0xc0) != 0x80) {
                ok = FALSE;
                break;
            }
        }
        if (!ok) {
            break;
        }
        s += n + 1;
    }
    if (ok && e && s != e) {
        ok = FALSE;     /* a NUL inside the given length */
    }
    if (end) {
        *end = (const gchar *)s;
    }
    return ok;
}

gint g_unichar_to_utf8(gunichar c, gchar *outbuf)
{
    gchar buf[6];
    gint n;

    if (!outbuf) {
        outbuf = buf;
    }
    if (c < 0x80) {
        outbuf[0] = (gchar)c;
        return 1;
    }
    if (c < 0x800) {
        outbuf[0] = (gchar)(0xc0 | (c >> 6));
        n = 2;
    } else if (c < 0x10000) {
        outbuf[0] = (gchar)(0xe0 | (c >> 12));
        n = 3;
    } else {
        outbuf[0] = (gchar)(0xf0 | (c >> 18));
        n = 4;
    }
    for (gint i = n - 1; i > 0; i--) {
        outbuf[i] = (gchar)(0x80 | (c & 0x3f));
        c >>= 6;
    }
    return n;
}

#ifdef _WIN32
gunichar2 *g_utf8_to_utf16(const gchar *str, glong len, glong *items_read,
                           glong *items_written, GError **error)
{
    gsize input_len = 0;
    gchar *tmp = NULL;
    wchar_t *w;

    if (!str) {
        g_set_error(error, 0, 0, "invalid UTF-8");
        return NULL;
    }
    while ((len < 0 || input_len < (gsize)len) && str[input_len]) {
        input_len++;
    }
    if (!input_len) {
        if (items_read) {
            *items_read = 0;
        }
        if (items_written) {
            *items_written = 0;
        }
        return g_new0(gunichar2, 1);
    }
    if (len >= 0) {
        tmp = g_new(gchar, input_len + 1);
        memcpy(tmp, str, input_len);
        tmp[input_len] = 0;
    }
    w = utf8_to_utf16_string_alloc(tmp ? tmp : str);

    g_free(tmp);
    if (!w) {
        g_set_error(error, 0, 0, "invalid UTF-8");
        return NULL;
    }
    if (items_written) {
        *items_written = (glong)wcslen(w);
    }
    if (items_read) {
        *items_read = (glong)input_len;
    }
    /* wchar_t is UTF-16 on Windows; transfer the converter allocation. */
    _Static_assert(sizeof(wchar_t) == sizeof(gunichar2), "UTF-16 unit size");
    return (gunichar2 *)w;
}

gchar *g_utf16_to_utf8(const gunichar2 *str, glong len, glong *items_read,
                       glong *items_written, GError **error)
{
    gsize input_len = 0;
    wchar_t *tmp = NULL;
    gchar *out;

    if (!str) {
        g_set_error(error, 0, 0, "invalid UTF-16");
        return NULL;
    }
    while ((len < 0 || input_len < (gsize)len) && str[input_len]) {
        input_len++;
    }
    if (!input_len) {
        if (items_read) {
            *items_read = 0;
        }
        if (items_written) {
            *items_written = 0;
        }
        return g_strdup("");
    }
    if (len >= 0) {
        tmp = g_new(wchar_t, input_len + 1);
        memcpy(tmp, str, input_len * sizeof(*tmp));
        tmp[input_len] = 0;
    }
    out = utf16_to_utf8_string_alloc(tmp ? tmp : (const wchar_t *)str);
    g_free(tmp);
    if (!out) {
        g_set_error(error, 0, 0, "invalid UTF-16");
    } else {
        if (items_written) {
            *items_written = (glong)strlen(out);
        }
        if (items_read) {
            *items_read = (glong)input_len;
        }
    }
    return out;
}
#endif

/* ---- base64, checksums ---------------------------------------------------- */

gchar *g_base64_encode(const guchar *data, gsize len)
{
    int n = 0;
    char *b = base64(data, (int)len, &n);
    gchar *out = g_strndup(b ? b : "", b ? (gsize)n : 0);

    free(b);
    return out;
}

guchar *g_base64_decode(const gchar *text, gsize *out_len)
{
    int n = 0;
    unsigned char *b = unbase64(text, (int)strlen(text), &n);
    gsize len = b ? (gsize)n : 0;
    guchar *out = g_malloc(len + 1);

    if (len) {
        memcpy(out, b, len);
    }
    out[len] = 0;
    free(b);
    *out_len = len;
    return out;
}

guchar *g_base64_decode_inplace(gchar *text, gsize *out_len)
{
    gsize n;
    guchar *d = g_base64_decode(text, &n);

    memcpy(text, d, n);
    g_free(d);
    *out_len = n;
    return (guchar *)text;
}

gchar *g_compute_checksum_for_data(GChecksumType checksum_type,
                                   const guchar *data, gsize length)
{
    uint8_t digest[20];
    gchar *hex;

    if (checksum_type != G_CHECKSUM_SHA1) {
        return NULL;
    }
    SHA1Digest(data, length, digest);
    hex = g_malloc(41);
    for (int i = 0; i < 20; i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return hex;
}

gchar *g_compute_checksum_for_string(GChecksumType checksum_type,
                                     const gchar *str, gssize length)
{
    return g_compute_checksum_for_data(checksum_type, (const guchar *)str,
                                       length < 0 ? strlen(str)
                                                  : (gsize)length);
}

guint g_parse_debug_string(const gchar *string, const GDebugKey *keys,
                           guint nkeys)
{
    guint result = 0;

    if (!string) {
        return 0;
    }
    if (!g_ascii_strcasecmp(string, "all")) {
        for (guint i = 0; i < nkeys; i++) {
            result |= keys[i].value;
        }
        return result;
    }
    for (guint i = 0; i < nkeys; i++) {
        const gchar *p = string;
        gsize klen = strlen(keys[i].key);
        while ((p = strstr(p, keys[i].key))) {
            if ((p == string || strchr(":;, \t", p[-1])) &&
                (!p[klen] || strchr(":;, \t", p[klen]))) {
                result |= keys[i].value;
                break;
            }
            p += klen;
        }
    }
    return result;
}

/* no shell in the core (libslirp's guestfwd commands): nothing parses */
gboolean g_shell_parse_argv(const gchar *command_line, gint *argcp,
                            gchar ***argvp, GError **error)
{
    g_set_error(error, 0, 0, "commands are not run by the libretro core");
    return FALSE;
}

/* ---- GString -------------------------------------------------------------- */

static void g_string_maybe_expand(GString *string, gsize len)
{
    if (string->len + len >= string->allocated_len) {
        gsize want = string->len + len + 1, a = 16;
        while (a < want) {
            a *= 2;
        }
        string->str = g_realloc(string->str, a);
        string->allocated_len = a;
    }
}

GString *g_string_sized_new(gsize dfl_size)
{
    GString *s = g_new(GString, 1);

    s->allocated_len = 0;
    s->len = 0;
    s->str = NULL;
    g_string_maybe_expand(s, MAX(dfl_size, 2));
    s->str[0] = 0;
    return s;
}

GString *g_string_new_len(const gchar *init, gssize len)
{
    GString *s;

    if (len < 0) {
        return g_string_new(init);
    }
    s = g_string_sized_new((gsize)len);
    if (init) {
        g_string_append_len(s, init, len);
    }
    return s;
}

GString *g_string_new(const gchar *init)
{
    GString *s;

    if (!init || !*init) {
        return g_string_sized_new(2);
    }
    s = g_string_sized_new(strlen(init) + 2);
    g_string_append_len(s, init, (gssize)strlen(init));
    return s;
}

GString *g_string_new_take(gchar *init)
{
    GString *s;

    if (!init) {
        return g_string_new(NULL);
    }
    s = g_new(GString, 1);
    s->str = init;
    s->len = strlen(init);
    s->allocated_len = s->len + 1;
    return s;
}

gchar *g_string_free(GString *string, gboolean free_segment)
{
    gchar *seg;

    if (!string) {
        return NULL;
    }
    seg = string->str;
    if (free_segment) {
        g_free(seg);
        seg = NULL;
    }
    g_free(string);
    return seg;
}

gboolean g_string_equal(const GString *v, const GString *v2)
{
    return v->len == v2->len && !memcmp(v->str, v2->str, v->len);
}

GString *g_string_truncate(GString *string, gsize len)
{
    string->len = MIN(len, string->len);
    string->str[string->len] = 0;
    return string;
}

GString *g_string_set_size(GString *string, gsize len)
{
    if (len >= string->allocated_len) {
        g_string_maybe_expand(string, len - string->len);
    }
    string->len = len;
    string->str[len] = 0;
    return string;
}

GString *g_string_insert_len(GString *string, gssize pos, const gchar *val,
                             gssize len)
{
    gsize l, p;

    if (len < 0) {
        len = (gssize)strlen(val);
    }
    l = (gsize)len;
    p = pos < 0 ? string->len : (gsize)pos;
    if (val >= string->str && val <= string->str + string->len) {
        /* a piece of itself: copied out first */
        gchar *copy = g_memdup2(val, l);
        g_string_insert_len(string, pos, copy, len);
        g_free(copy);
        return string;
    }
    g_string_maybe_expand(string, l);
    if (p < string->len) {
        memmove(string->str + p + l, string->str + p, string->len - p);
    }
    if (l) {
        memcpy(string->str + p, val, l);
    }
    string->len += l;
    string->str[string->len] = 0;
    return string;
}

GString *g_string_append_len(GString *string, const gchar *val, gssize len)
{
    return g_string_insert_len(string, -1, val, len);
}

GString *g_string_append(GString *string, const gchar *val)
{
    return g_string_insert_len(string, -1, val, -1);
}

GString *g_string_append_c(GString *string, gchar c)
{
    return g_string_insert_len(string, -1, &c, 1);
}

GString *g_string_append_unichar(GString *string, gunichar wc)
{
    gchar buf[6];
    gint n = g_unichar_to_utf8(wc, buf);

    return g_string_insert_len(string, -1, buf, n);
}

GString *g_string_prepend(GString *string, const gchar *val)
{
    return g_string_insert_len(string, 0, val, -1);
}

GString *g_string_prepend_c(GString *string, gchar c)
{
    return g_string_insert_len(string, 0, &c, 1);
}

GString *g_string_insert_c(GString *string, gssize pos, gchar c)
{
    return g_string_insert_len(string, pos, &c, 1);
}

GString *g_string_insert(GString *string, gssize pos, const gchar *val)
{
    return g_string_insert_len(string, pos, val, -1);
}

GString *g_string_assign(GString *string, const gchar *rval)
{
    if (string->str != rval) {
        g_string_truncate(string, 0);
        g_string_append(string, rval);
    }
    return string;
}

GString *g_string_erase(GString *string, gssize pos, gssize len)
{
    gsize p = (gsize)pos, l;

    l = (len < 0 || p + (gsize)len > string->len) ? string->len - p
                                                   : (gsize)len;
    memmove(string->str + p, string->str + p + l, string->len - p - l);
    string->len -= l;
    string->str[string->len] = 0;
    return string;
}

void g_string_append_vprintf(GString *string, const gchar *format,
                             va_list args)
{
    gchar *s;
    gint n = g_vasprintf(&s, format, args);

    if (n >= 0) {
        g_string_append_len(string, s, n);
    }
    g_free(s);
}

void g_string_append_printf(GString *string, const gchar *format, ...)
{
    va_list ap;

    va_start(ap, format);
    g_string_append_vprintf(string, format, ap);
    va_end(ap);
}

void g_string_vprintf(GString *string, const gchar *format, va_list args)
{
    g_string_truncate(string, 0);
    g_string_append_vprintf(string, format, args);
}

void g_string_printf(GString *string, const gchar *format, ...)
{
    va_list ap;

    g_string_truncate(string, 0);
    va_start(ap, format);
    g_string_append_vprintf(string, format, ap);
    va_end(ap);
}

/* ---- the system ----------------------------------------------------------- */

const gchar *g_getenv(const gchar *variable)
{
    return getenv(variable);
}

gboolean g_setenv(const gchar *variable, const gchar *value,
                  gboolean overwrite)
{
#ifdef _WIN32
    if (!overwrite && getenv(variable)) {
        return TRUE;
    }
    return _putenv_s(variable, value) == 0;
#else
    return setenv(variable, value, overwrite) == 0;
#endif
}

void g_unsetenv(const gchar *variable)
{
#ifdef _WIN32
    _putenv_s(variable, "");
#else
    unsetenv(variable);
#endif
}

static gchar *g_prgname;

const gchar *g_get_prgname(void)
{
    return g_prgname;
}

void g_set_prgname(const gchar *prgname)
{
    g_free(g_prgname);
    g_prgname = g_strdup(prgname);
}

const gchar *g_get_application_name(void)
{
    return g_prgname;
}

const gchar *g_get_tmp_dir(void)
{
    const gchar *t = getenv("TMPDIR");

#ifdef _WIN32
    if (!t) {
        t = getenv("TEMP");
    }
    if (!t) {
        t = getenv("TMP");
    }
    return t ? t : "C:\\";
#else
    return t && *t ? t : "/tmp";
#endif
}

const gchar *g_get_home_dir(void)
{
#ifdef _WIN32
    const gchar *h = getenv("USERPROFILE");
#else
    const gchar *h = getenv("HOME");
#endif
    return h ? h : "";
}

const gchar *g_get_user_name(void)
{
#ifdef _WIN32
    const gchar *u = getenv("USERNAME");
#else
    const gchar *u = getenv("USER");
#endif
    return u ? u : "libretro";
}

guint g_get_num_processors(void)
{
    unsigned n = cpu_features_get_core_amount();

    return n ? n : 1;
}

/* ---- time ----------------------------------------------------------------- */

gint64 g_get_monotonic_time(void)
{
    return (gint64)cpu_features_get_time_usec();
}

gint64 g_get_real_time(void)
{
#ifdef _WIN32
    FILETIME ft;
    guint64 t;

    GetSystemTimeAsFileTime(&ft);
    t = ((guint64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (gint64)(t / 10) - G_GINT64_CONSTANT(11644473600000000);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (gint64)tv.tv_sec * G_USEC_PER_SEC + tv.tv_usec;
#endif
}

void g_usleep(gulong microseconds)
{
    retro_sleep_us((unsigned)microseconds);
}

/* ---- random numbers (xoshiro128**, seeded as asked) ----------------------- */

struct _GRand {
    guint32 s[4];
};

static guint32 g_rotl(guint32 x, int k)
{
    return (x << k) | (x >> (32 - k));
}

GRand *g_rand_new_with_seed_array(const guint32 *seed, guint seed_length)
{
    GRand *r = g_new0(GRand, 1);
    guint32 x = 0x9e3779b9u;

    for (guint i = 0; i < 4; i++) {
        x ^= i < seed_length ? seed[i] : 0;
        /* splitmix-style spread, so a short seed fills the state */
        x += 0x9e3779b9u;
        guint32 z = x;
        z = (z ^ (z >> 16)) * 0x85ebca6bu;
        z = (z ^ (z >> 13)) * 0xc2b2ae35u;
        r->s[i] = z ^ (z >> 16);
    }
    if (!(r->s[0] | r->s[1] | r->s[2] | r->s[3])) {
        r->s[0] = 1;
    }
    return r;
}

GRand *g_rand_new_with_seed(guint32 seed)
{
    return g_rand_new_with_seed_array(&seed, 1);
}

GRand *g_rand_new(void)
{
    guint32 seed[2] = { (guint32)g_get_real_time(),
                        (guint32)g_get_monotonic_time() };

    return g_rand_new_with_seed_array(seed, 2);
}

void g_rand_free(GRand *rand_)
{
    g_free(rand_);
}

guint32 g_rand_int(GRand *r)
{
    guint32 result = g_rotl(r->s[1] * 5, 7) * 9, t = r->s[1] << 9;

    r->s[2] ^= r->s[0];
    r->s[3] ^= r->s[1];
    r->s[1] ^= r->s[2];
    r->s[0] ^= r->s[3];
    r->s[2] ^= t;
    r->s[3] = g_rotl(r->s[3], 11);
    return result;
}

gint32 g_rand_int_range(GRand *r, gint32 begin, gint32 end)
{
    guint32 span = (guint32)(end - begin);

    return span ? begin + (gint32)(g_rand_int(r) % span) : begin;
}

gdouble g_rand_double(GRand *r)
{
    return g_rand_int(r) / 4294967296.0;
}

/* Global draws use a seeded counter and the SplitMix64 output permutation.
 * Fetch-add reserves a unique counter without serializing a mutable GRand.
 * Seed publication is speculative: a stopped initializer cannot hold up a
 * caller, and losing seed candidates have no side effects to undo.
 */
#if !defined(RETRO_ATOMIC_LOCK_FREE) || !defined(RETRO_ATOMIC_HAS_64)
#error The global random generator requires lock-free 64-bit atomics
#endif
static retro_atomic_64_t g_random_seed;
static retro_atomic_64_t g_random_counter;

guint32 g_random_int(void)
{
    guint64 seed = (guint64)retro_atomic_load_acquire_64(&g_random_seed);
    guint64 value;

    if (!seed) {
        seed = (guint64)g_get_real_time() ^
               ((guint64)g_get_monotonic_time() << 32);
        if (!seed) {
            seed = 1;
        }
        if (!retro_atomic_cas_64(&g_random_seed, 0, (gint64)seed)) {
            seed = (guint64)retro_atomic_load_acquire_64(&g_random_seed);
        }
    }
    value = seed + ((guint64)retro_atomic_fetch_add_64(&g_random_counter, 1) + 1)
                   * UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (guint32)((value ^ (value >> 31)) >> 32);
}

gint32 g_random_int_range(gint32 begin, gint32 end)
{
    guint32 span, threshold, value;

    g_return_val_if_fail(end > begin, begin);
    span = (guint32)((gint64)end - begin);
    threshold = (guint32)(0u - span) % span;
    do {
        value = g_random_int();
    } while (value < threshold);
    return (gint32)((gint64)begin + value % span);
}

/* ---- once, on retro_atomic ------------------------------------------------ */

/* Once location registry. A hashed single-owner slot deadlocks when an
 * initializer enters a different location that happens to share its slot.
 * Track the actual addresses; never hold the registry lock in an initializer.
 */
typedef struct GOncePending {
    volatile void *location;
    struct GOncePending *next;
} GOncePending;
static GOncePending *g_once_pending;
static retro_atomic_int_t g_once_lock;

gboolean g_once_init_enter(volatile void *location)
{
    volatile gsize *loc = location;

    for (;;) {
        GOncePending *p;
        if (__atomic_load_n(loc, __ATOMIC_ACQUIRE)) {
            return FALSE;
        }
        g_compat_lock(&g_once_lock);
        if (__atomic_load_n(loc, __ATOMIC_ACQUIRE)) {
            g_compat_unlock(&g_once_lock);
            return FALSE;
        }
        for (p = g_once_pending; p && p->location != location; p = p->next) {
        }
        if (!p) {
            p = g_new(GOncePending, 1);
            p->location = location;
            p->next = g_once_pending;
            g_once_pending = p;
            g_compat_unlock(&g_once_lock);
            return TRUE;
        }
        g_compat_unlock(&g_once_lock);
        retro_sleep(0);
    }
}

void g_once_init_leave(volatile void *location, gsize result)
{
    GOncePending **link, *p;
    g_assert(result != 0);
    g_compat_lock(&g_once_lock);
    for (link = &g_once_pending; *link && (*link)->location != location;
         link = &(*link)->next) {
    }
    g_assert(*link != NULL);
    p = *link;
    *link = p->next;
    __atomic_store_n((volatile gsize *)location, result, __ATOMIC_RELEASE);
    g_compat_unlock(&g_once_lock);
    g_free(p);
}
