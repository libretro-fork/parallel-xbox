/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef XEMU_GL_TEXTURE_RECYCLE_H
#define XEMU_GL_TEXTURE_RECYCLE_H

/* Owner-context only. Limit both search cost and retained RGBA8 storage.
 * Keys are exact 11-bit width/height pairs, never hashes. Only single-level,
 * 2D RGBA8 uploads and complete 2D RGBA8 surface snapshots qualify. */
#define SAMPLED_TEXTURE_POOL_COUNT 16
#define SAMPLED_TEXTURE_POOL_BYTES (4u * 1024u * 1024u)
typedef struct SampledTexturePool {
    struct { GLuint texture; uint32_t key; uint64_t serial; } entries[SAMPLED_TEXTURE_POOL_COUNT];
    unsigned count, bytes;
    uint64_t submitted, completed;
} SampledTexturePool;

static inline uint32_t sampled_texture_key(unsigned w, unsigned h)
{
    return w && h && w <= 1024 && h <= 1024 ? (w << 11) | h : 0;
}

/* Explicit bytes fit the binding's existing padding on GCC's Windows ABI
 * too, where a uint32_t bitfield starts a new allocation unit. */
static inline uint32_t sampled_texture_key_load(const uint8_t key[3])
{
    return key[0] | ((uint32_t)key[1] << 8) | ((uint32_t)key[2] << 16);
}

static inline void sampled_texture_key_store(uint8_t key[3], uint32_t value)
{
    key[0] = value;
    key[1] = value >> 8;
    key[2] = value >> 16;
}

static inline unsigned sampled_texture_bytes(uint32_t key)
{
    return (key >> 11) * (key & 2047) * 4;
}

/* Optional warmup tickets outlive bindings; they must not sample a parked
 * object after the fence used for its retirement. No GL query is needed. */
static inline bool sampled_texture_contains(const SampledTexturePool *pool,
                                            GLuint texture)
{
    for (unsigned i = 0; i < pool->count; i++) {
        if (pool->entries[i].texture == texture) {
            return true;
        }
    }
    return false;
}

/* Keep optional pool/GL work out of the already large binding function. */
static __attribute__((noinline, unused)) GLuint sampled_texture_take(SampledTexturePool *pool, uint32_t key)
{
    if (!key) {
        return 0;
    }
    for (unsigned i = 0; i < pool->count; i++) {
        if (pool->entries[i].key == key &&
            pool->entries[i].serial <= pool->completed) {
            GLuint texture = pool->entries[i].texture;
            pool->bytes -= sampled_texture_bytes(key);
            pool->entries[i] = pool->entries[--pool->count];
            return texture;
        }
    }
    return 0;
}

static __attribute__((noinline, unused)) void sampled_texture_put(SampledTexturePool *pool, uint32_t key,
                                      GLuint texture)
{
    unsigned bytes = sampled_texture_bytes(key);
    if (key && pool->submitted != UINT64_MAX &&
        pool->count < SAMPLED_TEXTURE_POOL_COUNT &&
        bytes <= SAMPLED_TEXTURE_POOL_BYTES - pool->bytes) {
        pool->entries[pool->count].key = key;
        pool->entries[pool->count].serial = pool->submitted + 1;
        pool->entries[pool->count++].texture = texture;
        pool->bytes += bytes;
    } else {
        glDeleteTextures(1, &texture);
    }
}

/* Reuse only after a fence already polled by flip readiness covers the
 * release. An unsignaled fence or absent flips simply leaves a cache miss. */
static inline uint64_t sampled_texture_seal(SampledTexturePool *pool)
{
    assert(pool->submitted != UINT64_MAX);
    return ++pool->submitted;
}

static inline void sampled_texture_complete(SampledTexturePool *pool,
                                            uint64_t serial)
{
    if (serial > pool->completed) {
        pool->completed = serial;
    }
}

static __attribute__((noinline, unused)) void sampled_texture_reset(void)
{
    const GLint rgba[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, -1000.0f);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 1000.0f);
    /* apply_texture_parameters initializes filter/wrap/bias/anisotropy and
     * border color (when used) from the new binding's invalid shadows. */
}

/* Same direct pitched guest input as upload_gl_texture's linear RGBA8 path.
 * Only take() hits use this: storage already has the exact dimensions. */
static __attribute__((noinline, unused)) void sampled_texture_upload(unsigned w, unsigned h, unsigned pitch,
                                          const void *pixels)
{
    assert(pitch % 4 == 0);
    GLint alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA,
                    GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
}

static inline void sampled_texture_finalize(SampledTexturePool *pool)
{
    while (pool->count) {
        glDeleteTextures(1, &pool->entries[--pool->count].texture);
    }
    pool->bytes = 0;
    pool->submitted = pool->completed = 0;
}
static inline bool sampled_texture_rollover(SampledTexturePool *pool)
{
    if (pool->submitted != UINT64_MAX) {
        return false;
    }
    /* Drop optional retained objects rather than alias a new release with
     * an old fence after counter wrap. The caller clears its fence labels. */
    sampled_texture_finalize(pool);
    return true;
}
#endif
