/*
 * The part of GLib's API the libretro core uses, in-tree (libretro/glib/):
 * QEMU and libslirp are written against GLib, and the core links no GLib.
 * Types, macros and functions keep GLib's names and meanings for what is
 * here; what is not here is not used. The implementations are the .c
 * files beside this header, on libretro-common where it has the piece.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LR_GLIB_H
#define LR_GLIB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <signal.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the version this stands in for --------------------------------------- */

#define GLIB_MAJOR_VERSION 2
#define GLIB_MINOR_VERSION 66
#define GLIB_MICRO_VERSION 0
#define GLIB_CHECK_VERSION(major, minor, micro) \
    (GLIB_MAJOR_VERSION > (major) || \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION > (minor)) || \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION == (minor) && \
      GLIB_MICRO_VERSION >= (micro)))
#define G_ENCODE_VERSION(major, minor) ((major) << 16 | (minor) << 8)
#define GLIB_VERSION_2_56 G_ENCODE_VERSION(2, 56)
#define GLIB_VERSION_2_66 G_ENCODE_VERSION(2, 66)
#define GLIB_VERSION_2_68 G_ENCODE_VERSION(2, 68)
#define GLIB_VERSION_2_76 G_ENCODE_VERSION(2, 76)

/* ---- platform ------------------------------------------------------------- */

#ifdef _WIN32
#define G_OS_WIN32 1
#define G_PLATFORM_WIN32 1
#define G_DIR_SEPARATOR '\\'
#define G_DIR_SEPARATOR_S "\\"
#define G_IS_DIR_SEPARATOR(c) ((c) == '\\' || (c) == '/')
#define G_SEARCHPATH_SEPARATOR ';'
#else
#define G_OS_UNIX 1
#define G_DIR_SEPARATOR '/'
#define G_DIR_SEPARATOR_S "/"
#define G_IS_DIR_SEPARATOR(c) ((c) == '/')
#define G_SEARCHPATH_SEPARATOR ':'
#endif

#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN    4321
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define G_BYTE_ORDER G_BIG_ENDIAN
#else
#define G_BYTE_ORDER G_LITTLE_ENDIAN
#endif

#if UINTPTR_MAX == 0xffffffffffffffffull
#define GLIB_SIZEOF_VOID_P 8
#define GLIB_SIZEOF_SIZE_T 8
#else
#define GLIB_SIZEOF_VOID_P 4
#define GLIB_SIZEOF_SIZE_T 4
#endif
#if LONG_MAX == 0x7fffffffffffffffl
#define GLIB_SIZEOF_LONG 8
#else
#define GLIB_SIZEOF_LONG 4
#endif

/* ---- types ---------------------------------------------------------------- */

typedef char gchar;
typedef short gshort;
typedef long glong;
typedef int gint;
typedef gint gboolean;
typedef unsigned char guchar;
typedef unsigned short gushort;
typedef unsigned long gulong;
typedef unsigned int guint;
typedef float gfloat;
typedef double gdouble;
typedef void *gpointer;
typedef const void *gconstpointer;
typedef int8_t gint8;
typedef uint8_t guint8;
typedef int16_t gint16;
typedef uint16_t guint16;
typedef int32_t gint32;
typedef uint32_t guint32;
typedef int64_t gint64;
typedef uint64_t guint64;
typedef size_t gsize;
typedef ptrdiff_t gssize;
typedef intptr_t gintptr;
typedef uintptr_t guintptr;
typedef gint64 goffset;
typedef guint32 gunichar;
typedef guint16 gunichar2;
typedef guint32 GQuark;
typedef gchar **GStrv;
#ifdef _WIN32
typedef void *GPid;
#else
typedef int GPid;
#endif

#define G_MININT8  ((gint8)(-G_MAXINT8 - 1))
#define G_MAXINT8  ((gint8)0x7f)
#define G_MAXUINT8 ((guint8)0xff)
#define G_MININT16 ((gint16)(-G_MAXINT16 - 1))
#define G_MAXINT16 ((gint16)0x7fff)
#define G_MAXUINT16 ((guint16)0xffff)
#define G_MININT32 ((gint32)(-G_MAXINT32 - 1))
#define G_MAXINT32 ((gint32)0x7fffffff)
#define G_MAXUINT32 ((guint32)0xffffffff)
#define G_MININT64 ((gint64)(-G_MAXINT64 - 1))
#define G_MAXINT64 ((gint64)0x7fffffffffffffffll)
#define G_MAXUINT64 ((guint64)0xffffffffffffffffull)
#define G_MININT INT_MIN
#define G_MAXINT INT_MAX
#define G_MAXUINT UINT_MAX
#define G_MINLONG LONG_MIN
#define G_MAXLONG LONG_MAX
#define G_MAXULONG ULONG_MAX
#define G_MAXSIZE SIZE_MAX
#define G_MAXSSIZE PTRDIFF_MAX
#define G_MINSSIZE PTRDIFF_MIN
#define G_MAXDOUBLE DBL_MAX
#define G_MINDOUBLE DBL_MIN
#define G_MAXFLOAT FLT_MAX

#define G_GINT64_CONSTANT(v) (v##LL)
#define G_GUINT64_CONSTANT(v) (v##ULL)
#define G_GINT64_FORMAT "lld"
#define G_GUINT64_FORMAT "llu"
#define G_GSIZE_FORMAT "zu"
#define G_GSSIZE_FORMAT "zd"

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))
#endif
#ifndef CLAMP
#define CLAMP(x, low, high) \
    (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

#define GPOINTER_TO_INT(p) ((gint)(glong)(p))
#define GPOINTER_TO_UINT(p) ((guint)(gulong)(p))
#define GINT_TO_POINTER(i) ((gpointer)(glong)(i))
#define GUINT_TO_POINTER(u) ((gpointer)(gulong)(u))
#define GSIZE_TO_POINTER(s) ((gpointer)(guintptr)(s))
#define GPOINTER_TO_SIZE(p) ((gsize)(p))

#define GUINT16_SWAP_LE_BE(v) ((guint16)__builtin_bswap16((guint16)(v)))
#define GUINT32_SWAP_LE_BE(v) ((guint32)__builtin_bswap32((guint32)(v)))
#define GUINT64_SWAP_LE_BE(v) ((guint64)__builtin_bswap64((guint64)(v)))
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GUINT16_TO_BE(v) GUINT16_SWAP_LE_BE(v)
#define GUINT32_TO_BE(v) GUINT32_SWAP_LE_BE(v)
#define GUINT64_TO_BE(v) GUINT64_SWAP_LE_BE(v)
#define GUINT16_TO_LE(v) ((guint16)(v))
#define GUINT32_TO_LE(v) ((guint32)(v))
#define GUINT64_TO_LE(v) ((guint64)(v))
#else
#define GUINT16_TO_BE(v) ((guint16)(v))
#define GUINT32_TO_BE(v) ((guint32)(v))
#define GUINT64_TO_BE(v) ((guint64)(v))
#define GUINT16_TO_LE(v) GUINT16_SWAP_LE_BE(v)
#define GUINT32_TO_LE(v) GUINT32_SWAP_LE_BE(v)
#define GUINT64_TO_LE(v) GUINT64_SWAP_LE_BE(v)
#endif
#define GUINT16_FROM_BE(v) GUINT16_TO_BE(v)
#define GUINT32_FROM_BE(v) GUINT32_TO_BE(v)
#define GUINT64_FROM_BE(v) GUINT64_TO_BE(v)
#define GUINT16_FROM_LE(v) GUINT16_TO_LE(v)
#define GUINT32_FROM_LE(v) GUINT32_TO_LE(v)
#define GUINT64_FROM_LE(v) GUINT64_TO_LE(v)
#define GINT16_TO_BE(v) ((gint16)GUINT16_TO_BE(v))
#define GINT32_TO_BE(v) ((gint32)GUINT32_TO_BE(v))
#define GINT64_TO_BE(v) ((gint64)GUINT64_TO_BE(v))
#define GINT16_TO_LE(v) ((gint16)GUINT16_TO_LE(v))
#define GINT32_TO_LE(v) ((gint32)GUINT32_TO_LE(v))
#define GINT64_TO_LE(v) ((gint64)GUINT64_TO_LE(v))
#define GINT16_FROM_BE(v) GINT16_TO_BE(v)
#define GINT32_FROM_BE(v) GINT32_TO_BE(v)
#define GINT64_FROM_BE(v) GINT64_TO_BE(v)
#define GINT16_FROM_LE(v) GINT16_TO_LE(v)
#define GINT32_FROM_LE(v) GINT32_TO_LE(v)
#define GINT64_FROM_LE(v) GINT64_TO_LE(v)
#define g_htonl(v) GUINT32_TO_BE(v)
#define g_htons(v) GUINT16_TO_BE(v)
#define g_ntohl(v) GUINT32_FROM_BE(v)
#define g_ntohs(v) GUINT16_FROM_BE(v)

/* ---- compiler ------------------------------------------------------------- */

#define G_GNUC_EXTENSION __extension__
#define G_GNUC_UNUSED __attribute__((__unused__))
#define G_GNUC_NORETURN __attribute__((__noreturn__))
#define G_NORETURN __attribute__((__noreturn__))
#define G_GNUC_CONST __attribute__((__const__))
#define G_GNUC_PURE __attribute__((__pure__))
#define G_GNUC_MALLOC __attribute__((__malloc__))
#define G_GNUC_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#define G_GNUC_DEPRECATED __attribute__((__deprecated__))
#define G_GNUC_DEPRECATED_FOR(f) __attribute__((__deprecated__))
#define G_GNUC_FALLTHROUGH __attribute__((fallthrough))
#define G_GNUC_ALLOC_SIZE(x) __attribute__((__alloc_size__(x)))
#define G_GNUC_ALLOC_SIZE2(x, y) __attribute__((__alloc_size__(x, y)))
#define G_GNUC_FORMAT(n) __attribute__((__format_arg__(n)))
#if defined(__MINGW32__) && !defined(__clang__)
#define G_GNUC_PRINTF(f, a) __attribute__((__format__(gnu_printf, f, a)))
#define G_GNUC_SCANF(f, a) __attribute__((__format__(gnu_scanf, f, a)))
#else
#define G_GNUC_PRINTF(f, a) __attribute__((__format__(__printf__, f, a)))
#define G_GNUC_SCANF(f, a) __attribute__((__format__(__scanf__, f, a)))
#endif
#define G_LIKELY(e) __builtin_expect(!!(e), 1)
#define G_UNLIKELY(e) __builtin_expect(!!(e), 0)
#define G_STRFUNC ((const char *)(__func__))
#define G_STRLOC __FILE__ ":" G_STRINGIFY(__LINE__)
#define G_STRINGIFY_ARG(x) #x
#define G_STRINGIFY(x) G_STRINGIFY_ARG(x)
#define G_PASTE_ARGS(a, b) a##b
#define G_PASTE(a, b) G_PASTE_ARGS(a, b)
#define G_N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))
#define G_STRUCT_OFFSET(type, member) ((glong)offsetof(type, member))
#define G_SIZEOF_MEMBER(type, member) sizeof(((type *)0)->member)
#ifdef __cplusplus
#define G_STATIC_ASSERT(e) static_assert(e, #e)
#else
#define G_STATIC_ASSERT(e) _Static_assert(e, #e)
#endif
#define G_BEGIN_DECLS
#define G_END_DECLS
#define G_INLINE_FUNC static inline
#define GLIB_AVAILABLE_IN_ALL
#define G_DEPRECATED
#define G_DEPRECATED_FOR(f)
#define G_UNAVAILABLE(maj, min)
#define G_ANALYZER_NORETURN
#define G_USEC_PER_SEC 1000000

/* ---- memory --------------------------------------------------------------- */

gpointer g_malloc(gsize n_bytes) G_GNUC_MALLOC;
gpointer g_malloc0(gsize n_bytes) G_GNUC_MALLOC;
gpointer g_realloc(gpointer mem, gsize n_bytes);
gpointer g_malloc_n(gsize n_blocks, gsize n_block_bytes) G_GNUC_MALLOC;
gpointer g_malloc0_n(gsize n_blocks, gsize n_block_bytes) G_GNUC_MALLOC;
gpointer g_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes);
gpointer g_try_malloc(gsize n_bytes);
gpointer g_try_malloc0(gsize n_bytes);
gpointer g_try_malloc_n(gsize n_blocks, gsize n_block_bytes);
gpointer g_try_malloc0_n(gsize n_blocks, gsize n_block_bytes);
gpointer g_try_realloc(gpointer mem, gsize n_bytes);
void g_free(gpointer mem);
gpointer g_memdup(gconstpointer mem, guint byte_size);
gpointer g_memdup2(gconstpointer mem, gsize byte_size);

/* n of a type, as one allocation: the size checked inline, as GLib's
 * macros do, so that a program with only g_malloc (dspdiff) links */
static inline gsize g_n_bytes(gsize n, gsize size)
{
    gsize bytes;

    if (__builtin_mul_overflow(n, size, &bytes)) {
        abort();
    }
    return bytes;
}
#define g_new(type, n) ((type *)g_malloc(g_n_bytes((n), sizeof(type))))
#define g_new0(type, n) ((type *)g_malloc0(g_n_bytes((n), sizeof(type))))
#define g_renew(type, mem, n) \
    ((type *)g_realloc(mem, g_n_bytes((n), sizeof(type))))
#define g_try_new(type, n) ((type *)g_try_malloc_n((n), sizeof(type)))
#define g_try_new0(type, n) ((type *)g_try_malloc0_n((n), sizeof(type)))
#define g_newa(type, n) ((type *)__builtin_alloca(sizeof(type) * (gsize)(n)))
#define g_alloca(size) __builtin_alloca(size)

static inline gpointer g_steal_pointer_impl(gpointer pp)
{
    gpointer *p = (gpointer *)pp;
    gpointer ref = *p;
    *p = NULL;
    return ref;
}
#define g_steal_pointer(pp) \
    ((__typeof__(*(pp)))g_steal_pointer_impl(pp))

#define g_clear_pointer(pp, destroy) \
    do { \
        __typeof__((pp)) _pp = (pp); \
        __typeof__(*(pp)) _ptr = *_pp; \
        *_pp = NULL; \
        if (_ptr) { \
            (destroy)(_ptr); \
        } \
    } while (0)

/* g_autofree, g_autoptr, g_auto: what goes out of scope is freed */
static inline void g_autoptr_cleanup_generic_gfree(void *p)
{
    void **pp = (void **)p;
    g_free(*pp);
}
#define g_autofree __attribute__((cleanup(g_autoptr_cleanup_generic_gfree)))
#define _GLIB_AUTOPTR_FUNC_NAME(T) glib_autoptr_cleanup_##T
#define _GLIB_AUTOPTR_CLEAR_FUNC_NAME(T) glib_autoptr_clear_##T
#define _GLIB_AUTOPTR_TYPENAME(T) T##_autoptr
#define _GLIB_AUTO_FUNC_NAME(T) glib_auto_cleanup_##T
#define g_autoptr(T) \
    __attribute__((cleanup(_GLIB_AUTOPTR_FUNC_NAME(T)))) _GLIB_AUTOPTR_TYPENAME(T)
#define g_auto(T) __attribute__((cleanup(_GLIB_AUTO_FUNC_NAME(T)))) T
#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(T, func) \
    typedef T *_GLIB_AUTOPTR_TYPENAME(T); \
    static G_GNUC_UNUSED inline void _GLIB_AUTOPTR_CLEAR_FUNC_NAME(T)(T *_ptr) \
    { \
        if (_ptr) { \
            (func)(_ptr); \
        } \
    } \
    static G_GNUC_UNUSED inline void _GLIB_AUTOPTR_FUNC_NAME(T)(T **_ptr) \
    { \
        _GLIB_AUTOPTR_CLEAR_FUNC_NAME(T)(*_ptr); \
    }
#define G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(T, func) \
    static G_GNUC_UNUSED inline void _GLIB_AUTO_FUNC_NAME(T)(T *_ptr) \
    { \
        (func)(_ptr); \
    }
#define G_DEFINE_AUTO_CLEANUP_FREE_FUNC(T, func, none) \
    static G_GNUC_UNUSED inline void _GLIB_AUTO_FUNC_NAME(T)(T *_ptr) \
    { \
        if (*_ptr != none) { \
            (func)(*_ptr); \
        } \
    }

/* ---- messages and assertions ---------------------------------------------- */

typedef enum {
    G_LOG_FLAG_RECURSION = 1 << 0,
    G_LOG_FLAG_FATAL = 1 << 1,
    G_LOG_LEVEL_ERROR = 1 << 2,
    G_LOG_LEVEL_CRITICAL = 1 << 3,
    G_LOG_LEVEL_WARNING = 1 << 4,
    G_LOG_LEVEL_MESSAGE = 1 << 5,
    G_LOG_LEVEL_INFO = 1 << 6,
    G_LOG_LEVEL_DEBUG = 1 << 7,
    G_LOG_LEVEL_MASK = ~(G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL)
} GLogLevelFlags;

typedef void (*GLogFunc)(const gchar *log_domain, GLogLevelFlags log_level,
                         const gchar *message, gpointer user_data);
GLogFunc g_log_set_default_handler(GLogFunc log_func, gpointer user_data);
void g_log(const gchar *log_domain, GLogLevelFlags log_level,
           const gchar *format, ...) G_GNUC_PRINTF(3, 4);
void g_logv(const gchar *log_domain, GLogLevelFlags log_level,
            const gchar *format, va_list args) G_GNUC_PRINTF(3, 0);

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN ((gchar *)0)
#endif
#define g_error(...) \
    do { \
        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR, __VA_ARGS__); \
        abort(); \
    } while (0)
#define g_critical(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, __VA_ARGS__)
#define g_warning(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define g_message(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, __VA_ARGS__)
#define g_info(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __VA_ARGS__)
#define g_debug(...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define g_warning_once(...) \
    do { \
        static int _g_once_warned; \
        if (!_g_once_warned) { \
            _g_once_warned = 1; \
            g_warning(__VA_ARGS__); \
        } \
    } while (0)

G_NORETURN void g_assertion_message_expr(const char *domain, const char *file,
                                         int line, const char *func,
                                         const char *expr);
void g_return_if_fail_warning(const char *log_domain,
                              const char *pretty_function,
                              const char *expression);
void g_warn_message(const char *domain, const char *file, int line,
                    const char *func, const char *warnexpr);

#ifdef G_DISABLE_ASSERT
#define g_assert(expr) do { (void)0; } while (0)
#define g_assert_not_reached() __builtin_unreachable()
#else
#define g_assert(expr) \
    do { \
        if (G_LIKELY(expr)) { \
        } else { \
            g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, \
                                     G_STRFUNC, #expr); \
        } \
    } while (0)
#define g_assert_not_reached() \
    g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, NULL)
#endif
#define g_assert_true(expr) g_assert(expr)
#define g_assert_false(expr) g_assert(!(expr))
#define g_assert_null(expr) g_assert((expr) == NULL)
#define g_assert_nonnull(expr) g_assert((expr) != NULL)
#define g_assert_cmpint(a, op, b) g_assert((gint64)(a) op (gint64)(b))
#define g_assert_cmpuint(a, op, b) g_assert((guint64)(a) op (guint64)(b))
#define g_assert_cmphex(a, op, b) g_assert((guint64)(a) op (guint64)(b))
#define g_assert_cmpstr(a, op, b) g_assert(g_strcmp0((a), (b)) op 0)
#define g_return_if_fail(expr) \
    do { \
        if (G_LIKELY(expr)) { \
        } else { \
            g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, #expr); \
            return; \
        } \
    } while (0)
#define g_return_val_if_fail(expr, val) \
    do { \
        if (G_LIKELY(expr)) { \
        } else { \
            g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, #expr); \
            return (val); \
        } \
    } while (0)
#define g_return_if_reached() \
    do { \
        g_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, NULL); \
        return; \
    } while (0)
#define g_return_val_if_reached(val) \
    do { \
        g_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, NULL); \
        return (val); \
    } while (0)
#define g_warn_if_fail(expr) \
    do { \
        if (G_LIKELY(expr)) { \
        } else { \
            g_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, #expr); \
        } \
    } while (0)
#define g_warn_if_reached() \
    g_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, NULL)

/* the test framework: never initialized in the core */
#define g_test_initialized() FALSE
#define g_test_subprocess() FALSE
#define g_test_slow() FALSE
#define g_test_thorough() FALSE
#define g_test_quick() TRUE
#define g_test_message(...) do { } while (0)

/* ---- errors --------------------------------------------------------------- */

typedef struct _GError {
    GQuark domain;
    gint code;
    gchar *message;
} GError;

GError *g_error_new(GQuark domain, gint code, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);
void g_error_free(GError *error);
void g_set_error(GError **err, GQuark domain, gint code,
                 const gchar *format, ...) G_GNUC_PRINTF(4, 5);
void g_clear_error(GError **err);
GQuark g_quark_from_string(const gchar *string);
GQuark g_quark_from_static_string(const gchar *string);
GQuark g_quark_try_string(const gchar *string);
const gchar *g_quark_to_string(GQuark quark);
#define G_FILE_ERROR g_quark_from_static_string("g-file-error-quark")
#define G_SPAWN_ERROR g_quark_from_static_string("g-spawn-error-quark")
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GError, g_error_free)

/* ---- strings -------------------------------------------------------------- */

gchar *g_strdup(const gchar *str) G_GNUC_MALLOC;
gchar *g_strndup(const gchar *str, gsize n) G_GNUC_MALLOC;
gchar *g_strdup_printf(const gchar *format, ...) G_GNUC_PRINTF(1, 2)
    G_GNUC_MALLOC;
gchar *g_strdup_vprintf(const gchar *format, va_list args)
    G_GNUC_PRINTF(1, 0) G_GNUC_MALLOC;
gchar *g_strconcat(const gchar *string1, ...) G_GNUC_MALLOC
    G_GNUC_NULL_TERMINATED;
gint g_vasprintf(gchar **string, const gchar *format, va_list args)
    G_GNUC_PRINTF(2, 0);
gint g_snprintf(gchar *string, gulong n, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);
gint g_vsnprintf(gchar *string, gulong n, const gchar *format, va_list args)
    G_GNUC_PRINTF(3, 0);
#define g_printf printf
#define g_fprintf fprintf
#define g_sprintf sprintf
#define g_vprintf vprintf
#define g_vfprintf vfprintf
gsize g_strlcpy(gchar *dest, const gchar *src, gsize dest_size);
gsize g_strlcat(gchar *dest, const gchar *src, gsize dest_size);
gint g_strcmp0(const char *str1, const char *str2);
gboolean g_str_has_prefix(const gchar *str, const gchar *prefix);
gboolean g_str_has_suffix(const gchar *str, const gchar *suffix);
gchar *g_strstr_len(const gchar *haystack, gssize haystack_len,
                    const gchar *needle);
gchar *g_strrstr(const gchar *haystack, const gchar *needle);
gchar **g_strsplit(const gchar *string, const gchar *delimiter,
                   gint max_tokens) G_GNUC_MALLOC;
gchar *g_strjoinv(const gchar *separator, gchar **str_array) G_GNUC_MALLOC;
gchar *g_strjoin(const gchar *separator, ...) G_GNUC_MALLOC
    G_GNUC_NULL_TERMINATED;
void g_strfreev(gchar **str_array);
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(GStrv, g_strfreev, NULL)
guint g_strv_length(gchar **str_array);
gchar **g_strdupv(gchar **str_array);
gboolean g_strv_contains(const gchar *const *strv, const gchar *str);
gchar *g_strstrip(gchar *string);
gchar *g_strchug(gchar *string);
gchar *g_strchomp(gchar *string);
gchar *g_strcanon(gchar *string, const gchar *valid_chars,
                  gchar substitutor);
gchar *g_strdelimit(gchar *string, const gchar *delimiters,
                    gchar new_delimiter);
gchar *g_strreverse(gchar *string);
gchar *g_ascii_strdown(const gchar *str, gssize len) G_GNUC_MALLOC;
gchar *g_ascii_strup(const gchar *str, gssize len) G_GNUC_MALLOC;
gint g_ascii_strcasecmp(const gchar *s1, const gchar *s2);
gint g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n);
gint64 g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base);
guint64 g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base);
gdouble g_ascii_strtod(const gchar *nptr, gchar **endptr);
gint g_ascii_digit_value(gchar c);
gint g_ascii_xdigit_value(gchar c);
const gchar *g_strerror(gint errnum);
const gchar *g_strsignal(gint signum);
/* each argument read once, as a character (GLib's take a gchar too) */
static inline gboolean g_ascii_isdigit(gchar c)
{
    return c >= '0' && c <= '9';
}
static inline gboolean g_ascii_isupper(gchar c)
{
    return c >= 'A' && c <= 'Z';
}
static inline gboolean g_ascii_islower(gchar c)
{
    return c >= 'a' && c <= 'z';
}
static inline gboolean g_ascii_isalpha(gchar c)
{
    return g_ascii_isupper(c) || g_ascii_islower(c);
}
static inline gboolean g_ascii_isalnum(gchar c)
{
    return g_ascii_isalpha(c) || g_ascii_isdigit(c);
}
static inline gboolean g_ascii_isxdigit(gchar c)
{
    return g_ascii_isdigit(c) || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static inline gboolean g_ascii_isspace(gchar c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}
static inline gboolean g_ascii_isprint(gchar c)
{
    return c >= ' ' && c <= '~';
}
static inline gboolean g_ascii_isgraph(gchar c)
{
    return c > ' ' && c <= '~';
}
static inline gboolean g_ascii_iscntrl(gchar c)
{
    return (c >= 0 && c < ' ') || c == 127;
}
static inline gboolean g_ascii_ispunct(gchar c)
{
    return g_ascii_isgraph(c) && !g_ascii_isalnum(c);
}
gchar g_ascii_tolower(gchar c);
gchar g_ascii_toupper(gchar c);
#define G_CSET_A_2_Z "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define G_CSET_a_2_z "abcdefghijklmnopqrstuvwxyz"
#define G_CSET_DIGITS "0123456789"
gboolean g_pattern_match_simple(const gchar *pattern, const gchar *string);

gboolean g_utf8_validate(const gchar *str, gssize max_len, const gchar **end);
gint g_unichar_to_utf8(gunichar c, gchar *outbuf);
gunichar2 *g_utf8_to_utf16(const gchar *str, glong len, glong *items_read,
                           glong *items_written, GError **error);
gchar *g_utf16_to_utf8(const gunichar2 *str, glong len, glong *items_read,
                       glong *items_written, GError **error);

gchar *g_base64_encode(const guchar *data, gsize len) G_GNUC_MALLOC;
guchar *g_base64_decode(const gchar *text, gsize *out_len) G_GNUC_MALLOC;
guchar *g_base64_decode_inplace(gchar *text, gsize *out_len);

typedef enum {
    G_CHECKSUM_MD5,
    G_CHECKSUM_SHA1,
    G_CHECKSUM_SHA256,
    G_CHECKSUM_SHA512,
    G_CHECKSUM_SHA384
} GChecksumType;
gchar *g_compute_checksum_for_data(GChecksumType checksum_type,
                                   const guchar *data, gsize length);
gchar *g_compute_checksum_for_string(GChecksumType checksum_type,
                                     const gchar *str, gssize length);

typedef struct _GDebugKey {
    const gchar *key;
    guint value;
} GDebugKey;
guint g_parse_debug_string(const gchar *string, const GDebugKey *keys,
                           guint nkeys);
gboolean g_shell_parse_argv(const gchar *command_line, gint *argcp,
                            gchar ***argvp, GError **error);

/* ---- GString -------------------------------------------------------------- */

typedef struct _GString {
    gchar *str;
    gsize len;
    gsize allocated_len;
} GString;

GString *g_string_new(const gchar *init);
GString *g_string_new_len(const gchar *init, gssize len);
GString *g_string_new_take(gchar *init);
GString *g_string_sized_new(gsize dfl_size);
gchar *g_string_free(GString *string, gboolean free_segment);
GString *g_string_assign(GString *string, const gchar *rval);
GString *g_string_truncate(GString *string, gsize len);
GString *g_string_set_size(GString *string, gsize len);
GString *g_string_insert_len(GString *string, gssize pos, const gchar *val,
                             gssize len);
GString *g_string_append(GString *string, const gchar *val);
GString *g_string_append_len(GString *string, const gchar *val, gssize len);
GString *g_string_append_c(GString *string, gchar c);
GString *g_string_append_unichar(GString *string, gunichar wc);
GString *g_string_prepend(GString *string, const gchar *val);
GString *g_string_prepend_c(GString *string, gchar c);
GString *g_string_insert_c(GString *string, gssize pos, gchar c);
GString *g_string_insert(GString *string, gssize pos, const gchar *val);
GString *g_string_erase(GString *string, gssize pos, gssize len);
void g_string_printf(GString *string, const gchar *format, ...)
    G_GNUC_PRINTF(2, 3);
void g_string_vprintf(GString *string, const gchar *format, va_list args)
    G_GNUC_PRINTF(2, 0);
void g_string_append_printf(GString *string, const gchar *format, ...)
    G_GNUC_PRINTF(2, 3);
void g_string_append_vprintf(GString *string, const gchar *format,
                             va_list args) G_GNUC_PRINTF(2, 0);
gboolean g_string_equal(const GString *v, const GString *v2);
static inline gchar *g_string_free_and_steal(GString *string)
{
    return g_string_free(string, FALSE);
}
static inline void g_string_free_all(GString *string)
{
    g_string_free(string, TRUE);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GString, g_string_free_all)

/* ---- callbacks ------------------------------------------------------------ */

typedef void (*GDestroyNotify)(gpointer data);
typedef gint (*GCompareFunc)(gconstpointer a, gconstpointer b);
typedef gint (*GCompareDataFunc)(gconstpointer a, gconstpointer b,
                                 gpointer user_data);
typedef gboolean (*GEqualFunc)(gconstpointer a, gconstpointer b);
typedef gboolean (*GEqualFuncFull)(gconstpointer a, gconstpointer b,
                                   gpointer user_data);
typedef guint (*GHashFunc)(gconstpointer key);
typedef void (*GFunc)(gpointer data, gpointer user_data);
typedef void (*GHFunc)(gpointer key, gpointer value, gpointer user_data);
typedef gboolean (*GHRFunc)(gpointer key, gpointer value, gpointer user_data);
typedef gpointer (*GCopyFunc)(gconstpointer src, gpointer data);
typedef gboolean (*GTraverseFunc)(gpointer key, gpointer value,
                                  gpointer data);

/* ---- arrays --------------------------------------------------------------- */

typedef struct _GArray {
    gchar *data;
    guint len;
} GArray;
typedef struct _GByteArray {
    guint8 *data;
    guint len;
} GByteArray;
typedef struct _GPtrArray {
    gpointer *pdata;
    guint len;
} GPtrArray;

GArray *g_array_new(gboolean zero_terminated, gboolean clear,
                    guint element_size);
GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear,
                          guint element_size, guint reserved_size);
gchar *g_array_free(GArray *array, gboolean free_segment);
GArray *g_array_ref(GArray *array);
void g_array_unref(GArray *array);
guint g_array_get_element_size(GArray *array);
GArray *g_array_append_vals(GArray *array, gconstpointer data, guint len);
GArray *g_array_prepend_vals(GArray *array, gconstpointer data, guint len);
GArray *g_array_insert_vals(GArray *array, guint index, gconstpointer data,
                            guint len);
GArray *g_array_set_size(GArray *array, guint length);
GArray *g_array_remove_index(GArray *array, guint index);
GArray *g_array_remove_index_fast(GArray *array, guint index);
GArray *g_array_remove_range(GArray *array, guint index, guint length);
void g_array_sort(GArray *array, GCompareFunc compare_func);
void g_array_set_clear_func(GArray *array, GDestroyNotify clear_func);
#define g_array_append_val(a, v) g_array_append_vals(a, &(v), 1)
#define g_array_prepend_val(a, v) g_array_prepend_vals(a, &(v), 1)
#define g_array_insert_val(a, i, v) g_array_insert_vals(a, i, &(v), 1)
#define g_array_index(a, t, i) (((t *)(void *)(a)->data)[(i)])
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GArray, g_array_unref)

GByteArray *g_byte_array_new(void);
GByteArray *g_byte_array_sized_new(guint reserved_size);
GByteArray *g_byte_array_new_take(guint8 *data, gsize len);
guint8 *g_byte_array_free(GByteArray *array, gboolean free_segment);
void g_byte_array_unref(GByteArray *array);
GByteArray *g_byte_array_ref(GByteArray *array);
GByteArray *g_byte_array_append(GByteArray *array, const guint8 *data,
                                guint len);
GByteArray *g_byte_array_set_size(GByteArray *array, guint length);
GByteArray *g_byte_array_remove_range(GByteArray *array, guint index,
                                      guint length);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GByteArray, g_byte_array_unref)

GPtrArray *g_ptr_array_new(void);
GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func);
GPtrArray *g_ptr_array_new_full(guint reserved_size,
                                GDestroyNotify element_free_func);
GPtrArray *g_ptr_array_sized_new(guint reserved_size);
gpointer *g_ptr_array_free(GPtrArray *array, gboolean free_segment);
GPtrArray *g_ptr_array_ref(GPtrArray *array);
void g_ptr_array_unref(GPtrArray *array);
void g_ptr_array_set_free_func(GPtrArray *array,
                               GDestroyNotify element_free_func);
void g_ptr_array_set_size(GPtrArray *array, gint length);
void g_ptr_array_add(GPtrArray *array, gpointer data);
void g_ptr_array_insert(GPtrArray *array, gint index, gpointer data);
gboolean g_ptr_array_remove(GPtrArray *array, gpointer data);
gboolean g_ptr_array_remove_fast(GPtrArray *array, gpointer data);
gpointer g_ptr_array_remove_index(GPtrArray *array, guint index);
gpointer g_ptr_array_remove_index_fast(GPtrArray *array, guint index);
gpointer g_ptr_array_steal_index(GPtrArray *array, guint index);
void g_ptr_array_foreach(GPtrArray *array, GFunc func, gpointer user_data);
void g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func);
gboolean g_ptr_array_find(GPtrArray *haystack, gconstpointer needle,
                          guint *index_);
gboolean g_ptr_array_find_with_equal_func(GPtrArray *haystack,
                                          gconstpointer needle,
                                          GEqualFunc equal_func,
                                          guint *index_);
#define g_ptr_array_index(a, i) ((a)->pdata)[i]
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GPtrArray, g_ptr_array_unref)

/* ---- lists ---------------------------------------------------------------- */

typedef struct _GList GList;
struct _GList {
    gpointer data;
    GList *next;
    GList *prev;
};
typedef struct _GSList GSList;
struct _GSList {
    gpointer data;
    GSList *next;
};

GList *g_list_alloc(void);
void g_list_free(GList *list);
void g_list_free_1(GList *list);
void g_list_free_full(GList *list, GDestroyNotify free_func);
GList *g_list_append(GList *list, gpointer data);
GList *g_list_prepend(GList *list, gpointer data);
GList *g_list_insert(GList *list, gpointer data, gint position);
GList *g_list_insert_before(GList *list, GList *sibling, gpointer data);
GList *g_list_insert_sorted(GList *list, gpointer data, GCompareFunc func);
GList *g_list_remove(GList *list, gconstpointer data);
GList *g_list_remove_link(GList *list, GList *llink);
GList *g_list_delete_link(GList *list, GList *link_);
GList *g_list_reverse(GList *list);
GList *g_list_copy(GList *list);
GList *g_list_concat(GList *list1, GList *list2);
GList *g_list_find(GList *list, gconstpointer data);
GList *g_list_find_custom(GList *list, gconstpointer data, GCompareFunc func);
GList *g_list_first(GList *list);
GList *g_list_last(GList *list);
GList *g_list_nth(GList *list, guint n);
gpointer g_list_nth_data(GList *list, guint n);
guint g_list_length(GList *list);
void g_list_foreach(GList *list, GFunc func, gpointer user_data);
GList *g_list_sort(GList *list, GCompareFunc compare_func);
GList *g_list_sort_with_data(GList *list, GCompareDataFunc compare_func,
                             gpointer user_data);
#define g_list_next(l) ((l) ? (((GList *)(l))->next) : NULL)
#define g_list_previous(l) ((l) ? (((GList *)(l))->prev) : NULL)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GList, g_list_free)

GSList *g_slist_alloc(void);
void g_slist_free(GSList *list);
void g_slist_free_1(GSList *list);
void g_slist_free_full(GSList *list, GDestroyNotify free_func);
GSList *g_slist_append(GSList *list, gpointer data);
GSList *g_slist_prepend(GSList *list, gpointer data);
GSList *g_slist_insert_sorted(GSList *list, gpointer data, GCompareFunc func);
GSList *g_slist_remove(GSList *list, gconstpointer data);
GSList *g_slist_remove_link(GSList *list, GSList *link_);
GSList *g_slist_delete_link(GSList *list, GSList *link_);
GSList *g_slist_reverse(GSList *list);
GSList *g_slist_copy(GSList *list);
GSList *g_slist_concat(GSList *list1, GSList *list2);
GSList *g_slist_find(GSList *list, gconstpointer data);
GSList *g_slist_find_custom(GSList *list, gconstpointer data,
                            GCompareFunc func);
GSList *g_slist_last(GSList *list);
GSList *g_slist_nth(GSList *list, guint n);
gpointer g_slist_nth_data(GSList *list, guint n);
guint g_slist_length(GSList *list);
void g_slist_foreach(GSList *list, GFunc func, gpointer user_data);
GSList *g_slist_sort(GSList *list, GCompareFunc compare_func);
GSList *g_slist_sort_with_data(GSList *list, GCompareDataFunc compare_func,
                               gpointer user_data);
#define g_slist_next(l) ((l) ? (((GSList *)(l))->next) : NULL)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GSList, g_slist_free)

typedef struct _GQueue {
    GList *head;
    GList *tail;
    guint length;
} GQueue;
#define G_QUEUE_INIT { NULL, NULL, 0 }
GQueue *g_queue_new(void);
void g_queue_free(GQueue *queue);
void g_queue_free_full(GQueue *queue, GDestroyNotify free_func);
void g_queue_init(GQueue *queue);
void g_queue_clear(GQueue *queue);
gboolean g_queue_is_empty(GQueue *queue);
guint g_queue_get_length(GQueue *queue);
void g_queue_push_head(GQueue *queue, gpointer data);
void g_queue_push_tail(GQueue *queue, gpointer data);
gpointer g_queue_pop_head(GQueue *queue);
gpointer g_queue_pop_tail(GQueue *queue);
gpointer g_queue_peek_head(GQueue *queue);
gpointer g_queue_peek_tail(GQueue *queue);
gboolean g_queue_remove(GQueue *queue, gconstpointer data);
void g_queue_foreach(GQueue *queue, GFunc func, gpointer user_data);

/* ---- hash tables ---------------------------------------------------------- */

typedef struct _GHashTable GHashTable;
typedef struct _GHashTableIter {
    gpointer dummy1;
    gpointer dummy2;
    gpointer dummy3;
    int dummy4;
    gboolean dummy5;
    gpointer dummy6;
} GHashTableIter;

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func);
GHashTable *g_hash_table_new_full(GHashFunc hash_func,
                                  GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func);
void g_hash_table_destroy(GHashTable *hash_table);
GHashTable *g_hash_table_ref(GHashTable *hash_table);
void g_hash_table_unref(GHashTable *hash_table);
gboolean g_hash_table_insert(GHashTable *hash_table, gpointer key,
                             gpointer value);
gboolean g_hash_table_replace(GHashTable *hash_table, gpointer key,
                              gpointer value);
gboolean g_hash_table_add(GHashTable *hash_table, gpointer key);
gboolean g_hash_table_remove(GHashTable *hash_table, gconstpointer key);
void g_hash_table_remove_all(GHashTable *hash_table);
gboolean g_hash_table_steal(GHashTable *hash_table, gconstpointer key);
gboolean g_hash_table_steal_extended(GHashTable *hash_table,
                                     gconstpointer lookup_key,
                                     gpointer *stolen_key,
                                     gpointer *stolen_value);
gpointer g_hash_table_lookup(GHashTable *hash_table, gconstpointer key);
gboolean g_hash_table_contains(GHashTable *hash_table, gconstpointer key);
gboolean g_hash_table_lookup_extended(GHashTable *hash_table,
                                      gconstpointer lookup_key,
                                      gpointer *orig_key, gpointer *value);
void g_hash_table_foreach(GHashTable *hash_table, GHFunc func,
                          gpointer user_data);
gpointer g_hash_table_find(GHashTable *hash_table, GHRFunc predicate,
                           gpointer user_data);
guint g_hash_table_foreach_remove(GHashTable *hash_table, GHRFunc func,
                                  gpointer user_data);
guint g_hash_table_foreach_steal(GHashTable *hash_table, GHRFunc func,
                                 gpointer user_data);
guint g_hash_table_size(GHashTable *hash_table);
GList *g_hash_table_get_keys(GHashTable *hash_table);
GList *g_hash_table_get_values(GHashTable *hash_table);
void g_hash_table_iter_init(GHashTableIter *iter, GHashTable *hash_table);
gboolean g_hash_table_iter_next(GHashTableIter *iter, gpointer *key,
                                gpointer *value);
void g_hash_table_iter_remove(GHashTableIter *iter);
void g_hash_table_iter_steal(GHashTableIter *iter);
GHashTable *g_hash_table_iter_get_hash_table(GHashTableIter *iter);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GHashTable, g_hash_table_unref)

guint g_str_hash(gconstpointer v);
gboolean g_str_equal(gconstpointer v1, gconstpointer v2);
guint g_direct_hash(gconstpointer v);
gboolean g_direct_equal(gconstpointer v1, gconstpointer v2);
guint g_int_hash(gconstpointer v);
gboolean g_int_equal(gconstpointer v1, gconstpointer v2);
guint g_int64_hash(gconstpointer v);
gboolean g_int64_equal(gconstpointer v1, gconstpointer v2);

/* ---- balanced trees and sequences ----------------------------------------- */

typedef struct _GTree GTree;
typedef struct _GTreeNode GTreeNode;
GTree *g_tree_new(GCompareFunc key_compare_func);
GTree *g_tree_new_with_data(GCompareDataFunc key_compare_func,
                            gpointer key_compare_data);
GTree *g_tree_new_full(GCompareDataFunc key_compare_func,
                       gpointer key_compare_data,
                       GDestroyNotify key_destroy_func,
                       GDestroyNotify value_destroy_func);
GTree *g_tree_ref(GTree *tree);
void g_tree_unref(GTree *tree);
void g_tree_destroy(GTree *tree);
void g_tree_insert(GTree *tree, gpointer key, gpointer value);
void g_tree_replace(GTree *tree, gpointer key, gpointer value);
gboolean g_tree_remove(GTree *tree, gconstpointer key);
gboolean g_tree_steal(GTree *tree, gconstpointer key);
gpointer g_tree_lookup(GTree *tree, gconstpointer key);
gboolean g_tree_lookup_extended(GTree *tree, gconstpointer lookup_key,
                                gpointer *orig_key, gpointer *value);
void g_tree_foreach(GTree *tree, GTraverseFunc func, gpointer user_data);
gpointer g_tree_search(GTree *tree, GCompareFunc search_func,
                       gconstpointer user_data);
gint g_tree_nnodes(GTree *tree);
gint g_tree_height(GTree *tree);
GTreeNode *g_tree_node_first(GTree *tree);
GTreeNode *g_tree_node_last(GTree *tree);
GTreeNode *g_tree_node_next(GTreeNode *node);
GTreeNode *g_tree_node_previous(GTreeNode *node);
gpointer g_tree_node_key(GTreeNode *node);
gpointer g_tree_node_value(GTreeNode *node);
GTreeNode *g_tree_lookup_node(GTree *tree, gconstpointer key);
GTreeNode *g_tree_insert_node(GTree *tree, gpointer key, gpointer value);
GTreeNode *g_tree_lower_bound(GTree *tree, gconstpointer key);
GTreeNode *g_tree_upper_bound(GTree *tree, gconstpointer key);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GTree, g_tree_unref)

typedef struct _GSequence GSequence;
typedef struct _GSequenceNode GSequenceIter;
GSequence *g_sequence_new(GDestroyNotify data_destroy);
void g_sequence_free(GSequence *seq);
GSequenceIter *g_sequence_insert_sorted(GSequence *seq, gpointer data,
                                        GCompareDataFunc cmp_func,
                                        gpointer cmp_data);
GSequenceIter *g_sequence_lookup(GSequence *seq, gpointer data,
                                 GCompareDataFunc cmp_func,
                                 gpointer cmp_data);
void g_sequence_remove(GSequenceIter *iter);
gint g_sequence_get_length(GSequence *seq);
gpointer g_sequence_get(GSequenceIter *iter);

/* keyed data on an object (GData) */
typedef struct _GData GData;
gpointer g_datalist_get_data(GData **datalist, const gchar *key);
void g_datalist_id_set_data_full(GData **datalist, GQuark key_id,
                                 gpointer data, GDestroyNotify destroy_func);
void g_datalist_clear(GData **datalist);
#define g_datalist_init(dl) (*(dl) = NULL)
#define g_datalist_set_data(dl, k, d) \
    g_datalist_id_set_data_full((dl), g_quark_from_string(k), (d), NULL)
#define g_datalist_set_data_full(dl, k, d, f) \
    g_datalist_id_set_data_full((dl), g_quark_from_string(k), (d), (f))
#define g_datalist_remove_data(dl, k) \
    g_datalist_id_set_data_full((dl), g_quark_try_string(k), NULL, NULL)

/* ---- threads -------------------------------------------------------------- */

typedef enum {
    G_ONCE_STATUS_NOTCALLED,
    G_ONCE_STATUS_PROGRESS,
    G_ONCE_STATUS_READY
} GOnceStatus;
typedef struct _GOnce {
    volatile GOnceStatus status;
    volatile gpointer retval;
} GOnce;
#define G_ONCE_INIT { G_ONCE_STATUS_NOTCALLED, NULL }
typedef gpointer (*GThreadFunc)(gpointer data);
gpointer g_once_impl(GOnce *once, GThreadFunc func, gpointer arg);
#define g_once(once, func, arg) g_once_impl((once), (func), (arg))
gboolean g_once_init_enter(volatile void *location);
void g_once_init_leave(volatile void *location, gsize result);

/* ---- the main loop -------------------------------------------------------- */

typedef enum {
    G_IO_IN = 1,
    G_IO_PRI = 2,
    G_IO_OUT = 4,
    G_IO_ERR = 8,
    G_IO_HUP = 16,
    G_IO_NVAL = 32
} GIOCondition;

typedef struct _GPollFD {
#if defined(_WIN32) && GLIB_SIZEOF_VOID_P == 8
    gint64 fd;
#else
    gint fd;
#endif
    gushort events;
    gushort revents;
} GPollFD;
typedef gint (*GPollFunc)(GPollFD *ufds, guint nfsd, gint timeout_);
gint g_poll(GPollFD *fds, guint nfds, gint timeout);

#define G_PRIORITY_HIGH -100
#define G_PRIORITY_DEFAULT 0
#define G_PRIORITY_HIGH_IDLE 100
#define G_PRIORITY_DEFAULT_IDLE 200
#define G_PRIORITY_LOW 300
#define G_SOURCE_REMOVE FALSE
#define G_SOURCE_CONTINUE TRUE

typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;
typedef struct _GSource GSource;
typedef struct _GSourceFuncs GSourceFuncs;
typedef gboolean (*GSourceFunc)(gpointer user_data);
typedef void (*GSourceDummyMarshal)(void);
struct _GSourceFuncs {
    gboolean (*prepare)(GSource *source, gint *timeout_);
    gboolean (*check)(GSource *source);
    gboolean (*dispatch)(GSource *source, GSourceFunc callback,
                         gpointer user_data);
    void (*finalize)(GSource *source);
    GSourceFunc closure_callback;
    GSourceDummyMarshal closure_marshal;
};
/* a source as the main loop keeps it; its own data follows (g_source_new) */
struct _GSource {
    GSourceFuncs *source_funcs;
    GMainContext *context;
    GSourceFunc callback;
    gpointer callback_data;
    GDestroyNotify callback_notify;
    gint ref_count;
    guint source_id;
    guint flags;
    gint priority;
    gboolean can_recurse;
    gboolean in_dispatch;
    gint64 ready_time;
    GSList *poll_fds;
    GSList *unix_fds;       /* the ones g_source_add_unix_fd made */
    GSList *child_sources;
    GSource *parent;
    GSource *next;          /* in its context's list */
    gchar *name;
    gpointer callback_owner; /* private reference-counted callback state */
};

GMainContext *g_main_context_new(void);
GMainContext *g_main_context_ref(GMainContext *context);
void g_main_context_unref(GMainContext *context);
GMainContext *g_main_context_default(void);
GMainContext *g_main_context_get_thread_default(void);
GMainContext *g_main_context_ref_thread_default(void);
void g_main_context_push_thread_default(GMainContext *context);
void g_main_context_pop_thread_default(GMainContext *context);
gboolean g_main_context_acquire(GMainContext *context);
void g_main_context_release(GMainContext *context);
gboolean g_main_context_is_owner(GMainContext *context);
gboolean g_main_context_prepare(GMainContext *context, gint *priority);
gint g_main_context_query(GMainContext *context, gint max_priority,
                          gint *timeout_, GPollFD *fds, gint n_fds);
gboolean g_main_context_check(GMainContext *context, gint max_priority,
                              GPollFD *fds, gint n_fds);
void g_main_context_dispatch(GMainContext *context);
gboolean g_main_context_iteration(GMainContext *context, gboolean may_block);
gboolean g_main_context_pending(GMainContext *context);
void g_main_context_wakeup(GMainContext *context);
GSource *g_main_context_find_source_by_id(GMainContext *context,
                                          guint source_id);

GMainLoop *g_main_loop_new(GMainContext *context, gboolean is_running);
GMainLoop *g_main_loop_ref(GMainLoop *loop);
void g_main_loop_unref(GMainLoop *loop);
void g_main_loop_run(GMainLoop *loop);
void g_main_loop_quit(GMainLoop *loop);
gboolean g_main_loop_is_running(GMainLoop *loop);
GMainContext *g_main_loop_get_context(GMainLoop *loop);

GSource *g_source_new(GSourceFuncs *source_funcs, guint struct_size);
GSource *g_source_ref(GSource *source);
void g_source_unref(GSource *source);
guint g_source_attach(GSource *source, GMainContext *context);
void g_source_destroy(GSource *source);
gboolean g_source_is_destroyed(GSource *source);
gboolean g_source_remove(guint tag);
void g_source_set_callback(GSource *source, GSourceFunc func, gpointer data,
                           GDestroyNotify notify);
void g_source_set_priority(GSource *source, gint priority);
gint g_source_get_priority(GSource *source);
void g_source_set_can_recurse(GSource *source, gboolean can_recurse);
void g_source_set_name(GSource *source, const char *name);
const char *g_source_get_name(GSource *source);
void g_source_set_ready_time(GSource *source, gint64 ready_time);
guint g_source_get_id(GSource *source);
GMainContext *g_source_get_context(GSource *source);
void g_source_add_poll(GSource *source, GPollFD *fd);
void g_source_remove_poll(GSource *source, GPollFD *fd);
gpointer g_source_add_unix_fd(GSource *source, gint fd, GIOCondition events);
void g_source_modify_unix_fd(GSource *source, gpointer tag,
                             GIOCondition new_events);
void g_source_remove_unix_fd(GSource *source, gpointer tag);
GIOCondition g_source_query_unix_fd(GSource *source, gpointer tag);
void g_source_add_child_source(GSource *source, GSource *child_source);
void g_source_remove_child_source(GSource *source, GSource *child_source);
gint64 g_source_get_time(GSource *source);
GSource *g_idle_source_new(void);
GSource *g_timeout_source_new(guint interval);
GSource *g_timeout_source_new_seconds(guint interval);
guint g_idle_add(GSourceFunc function, gpointer data);
guint g_idle_add_full(gint priority, GSourceFunc function, gpointer data,
                      GDestroyNotify notify);
guint g_timeout_add(guint interval, GSourceFunc function, gpointer data);
guint g_timeout_add_full(gint priority, guint interval, GSourceFunc function,
                         gpointer data, GDestroyNotify notify);
guint g_timeout_add_seconds(guint interval, GSourceFunc function,
                            gpointer data);
static inline void g_clear_handle_id_impl(guint *tag_ptr,
                                          gboolean (*clear_func)(guint))
{
    guint tag = *tag_ptr;
    *tag_ptr = 0;
    if (tag) {
        clear_func(tag);
    }
}
#define g_clear_handle_id(tag_ptr, clear_func) \
    g_clear_handle_id_impl((tag_ptr), (gboolean (*)(guint))(clear_func))
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GSource, g_source_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMainContext, g_main_context_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMainLoop, g_main_loop_unref)

/* IO channels: only the type, for function pointers that name it */
typedef struct _GIOChannel GIOChannel;
typedef gboolean (*GIOFunc)(GIOChannel *source, GIOCondition condition,
                            gpointer data);

/* ---- time ----------------------------------------------------------------- */

gint64 g_get_monotonic_time(void);
gint64 g_get_real_time(void);
void g_usleep(gulong microseconds);

typedef struct _GTimer GTimer;
GTimer *g_timer_new(void);
void g_timer_destroy(GTimer *timer);
void g_timer_start(GTimer *timer);
void g_timer_stop(GTimer *timer);
void g_timer_reset(GTimer *timer);
gdouble g_timer_elapsed(GTimer *timer, gulong *microseconds);

typedef struct _GDateTime GDateTime;
typedef gint64 GTimeSpan;
GDateTime *g_date_time_new_now_local(void);
GDateTime *g_date_time_new_now_utc(void);
GDateTime *g_date_time_new_from_unix_utc(gint64 t);
GDateTime *g_date_time_new_from_unix_local(gint64 t);
void g_date_time_unref(GDateTime *datetime);
GDateTime *g_date_time_ref(GDateTime *datetime);
gint64 g_date_time_to_unix(GDateTime *datetime);
gint g_date_time_get_microsecond(GDateTime *datetime);
gchar *g_date_time_format(GDateTime *datetime, const gchar *format);
gchar *g_date_time_format_iso8601(GDateTime *datetime);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GDateTime, g_date_time_unref)

/* ---- random numbers ------------------------------------------------------- */

typedef struct _GRand GRand;
GRand *g_rand_new(void);
GRand *g_rand_new_with_seed(guint32 seed);
GRand *g_rand_new_with_seed_array(const guint32 *seed, guint seed_length);
void g_rand_free(GRand *rand_);
guint32 g_rand_int(GRand *rand_);
gint32 g_rand_int_range(GRand *rand_, gint32 begin, gint32 end);
gdouble g_rand_double(GRand *rand_);
guint32 g_random_int(void);
gint32 g_random_int_range(gint32 begin, gint32 end);

/* ---- the system ----------------------------------------------------------- */

const gchar *g_getenv(const gchar *variable);
gboolean g_setenv(const gchar *variable, const gchar *value,
                  gboolean overwrite);
void g_unsetenv(const gchar *variable);
const gchar *g_get_prgname(void);
void g_set_prgname(const gchar *prgname);
const gchar *g_get_application_name(void);
const gchar *g_get_tmp_dir(void);
const gchar *g_get_home_dir(void);
const gchar *g_get_user_name(void);
gchar *g_get_current_dir(void);
const gchar *const *g_get_system_data_dirs(void);
#ifdef _WIN32
gchar *g_win32_error_message(gint error);
#endif
guint g_get_num_processors(void);

/* ---- files ---------------------------------------------------------------- */

typedef enum {
    G_FILE_TEST_IS_REGULAR = 1 << 0,
    G_FILE_TEST_IS_SYMLINK = 1 << 1,
    G_FILE_TEST_IS_DIR = 1 << 2,
    G_FILE_TEST_IS_EXECUTABLE = 1 << 3,
    G_FILE_TEST_EXISTS = 1 << 4
} GFileTest;
typedef enum {
    G_FILE_ERROR_EXIST,
    G_FILE_ERROR_ISDIR,
    G_FILE_ERROR_ACCES,
    G_FILE_ERROR_NAMETOOLONG,
    G_FILE_ERROR_NOENT,
    G_FILE_ERROR_NOTDIR,
    G_FILE_ERROR_FAILED = 24
} GFileError;

gboolean g_file_test(const gchar *filename, GFileTest test);
gboolean g_file_get_contents(const gchar *filename, gchar **contents,
                             gsize *length, GError **error);
gboolean g_file_set_contents(const gchar *filename, const gchar *contents,
                             gssize length, GError **error);
gchar *g_file_read_link(const gchar *filename, GError **error);
gint g_mkdir_with_parents(const gchar *pathname, gint mode);
gchar *g_dir_make_tmp(const gchar *tmpl, GError **error);
gint g_file_open_tmp(const gchar *tmpl, gchar **name_used, GError **error);
gchar *g_path_get_basename(const gchar *file_name) G_GNUC_MALLOC;
gchar *g_path_get_dirname(const gchar *file_name) G_GNUC_MALLOC;
gboolean g_path_is_absolute(const gchar *file_name);
gchar *g_build_filename(const gchar *first_element, ...) G_GNUC_MALLOC
    G_GNUC_NULL_TERMINATED;
gchar *g_build_path(const gchar *separator, const gchar *first_element, ...)
    G_GNUC_MALLOC G_GNUC_NULL_TERMINATED;
gchar *g_find_program_in_path(const gchar *program);
gchar *g_canonicalize_filename(const gchar *filename,
                               const gchar *relative_to);

typedef struct _GDir GDir;
GDir *g_dir_open(const gchar *path, guint flags, GError **error);
const gchar *g_dir_read_name(GDir *dir);
void g_dir_close(GDir *dir);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GDir, g_dir_close)

typedef struct _GMappedFile GMappedFile;
GMappedFile *g_mapped_file_new(const gchar *filename, gboolean writable,
                               GError **error);
GMappedFile *g_mapped_file_new_from_fd(gint fd, gboolean writable,
                                       GError **error);
gsize g_mapped_file_get_length(GMappedFile *file);
gchar *g_mapped_file_get_contents(GMappedFile *file);
GMappedFile *g_mapped_file_ref(GMappedFile *file);
void g_mapped_file_unref(GMappedFile *file);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMappedFile, g_mapped_file_unref)

/* ---- processes (none are started in the core) ----------------------------- */

typedef enum {
    G_SPAWN_DEFAULT = 0,
    G_SPAWN_LEAVE_DESCRIPTORS_OPEN = 1 << 0,
    G_SPAWN_DO_NOT_REAP_CHILD = 1 << 1,
    G_SPAWN_SEARCH_PATH = 1 << 2,
    G_SPAWN_STDOUT_TO_DEV_NULL = 1 << 3,
    G_SPAWN_STDERR_TO_DEV_NULL = 1 << 4,
    G_SPAWN_CHILD_INHERITS_STDIN = 1 << 5,
    G_SPAWN_FILE_AND_ARGV_ZERO = 1 << 6,
    G_SPAWN_SEARCH_PATH_FROM_ENVP = 1 << 7,
    G_SPAWN_CLOEXEC_PIPES = 1 << 8
} GSpawnFlags;
typedef void (*GSpawnChildSetupFunc)(gpointer user_data);
gboolean g_spawn_async(const gchar *working_directory, gchar **argv,
                       gchar **envp, GSpawnFlags flags,
                       GSpawnChildSetupFunc child_setup, gpointer user_data,
                       GPid *child_pid, GError **error);
gboolean g_spawn_async_with_fds(const gchar *working_directory, gchar **argv,
                                gchar **envp, GSpawnFlags flags,
                                GSpawnChildSetupFunc child_setup,
                                gpointer user_data, GPid *child_pid,
                                gint stdin_fd, gint stdout_fd, gint stderr_fd,
                                GError **error);
gboolean g_spawn_async_with_pipes(const gchar *working_directory,
                                  gchar **argv, gchar **envp,
                                  GSpawnFlags flags,
                                  GSpawnChildSetupFunc child_setup,
                                  gpointer user_data, GPid *child_pid,
                                  gint *standard_input, gint *standard_output,
                                  gint *standard_error, GError **error);
void g_spawn_close_pid(GPid pid);

#ifdef __cplusplus
}
#endif

#endif /* LR_GLIB_H */
