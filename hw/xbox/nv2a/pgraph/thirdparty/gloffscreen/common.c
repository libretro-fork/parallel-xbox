/*
 *  Offscreen OpenGL abstraction layer - Common utilities
 *
 *  Copyright (c) 2010 Intel
 *  Written by:
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <features/features_cpu.h>
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define GLO_HAVE_AVX2_SWAP
#define GLO_HAVE_SSE2_SWAP
#endif

#include "gloffscreen.h"


/* Keep the tuned libc copy path on targets without an in-place vector
 * swap: scalar swapping regresses large rows on some targets. */
static void glo_flip_rows_portable(uint8_t *data, unsigned stride, size_t row_bytes,
                                   unsigned height)
{
    if (height < 2 || !row_bytes) {
        return;
    }
    uint8_t *tmp = malloc(row_bytes);
    for (unsigned y = 0; y < height / 2; y++) {
        uint8_t *top = data + (size_t)y * stride;
        uint8_t *bottom = data + (size_t)(height - 1 - y) * stride;
        memcpy(tmp, top, row_bytes);
        memcpy(top, bottom, row_bytes);
        memcpy(bottom, tmp, row_bytes);
    }
    free(tmp);
}

#ifdef GLO_HAVE_SSE2_SWAP
__attribute__((target("sse2")))
static void glo_flip_rows_sse2(uint8_t *data, unsigned stride, size_t row_bytes,
                              unsigned height)
{
    if (height < 2 || !row_bytes) {
        return;
    }
    for (unsigned y = 0; y < height / 2; y++) {
        uint8_t *top = data + (size_t)y * stride;
        uint8_t *bottom = data + (size_t)(height - 1 - y) * stride;
        size_t x = 0;
        for (; row_bytes - x >= 64; x += 64) {
            __m128i a = _mm_loadu_si128((const __m128i *)(top + x));
            __m128i b = _mm_loadu_si128((const __m128i *)(bottom + x));
            __m128i c = _mm_loadu_si128((const __m128i *)(top + x + 16));
            __m128i d = _mm_loadu_si128((const __m128i *)(bottom + x + 16));
            _mm_storeu_si128((__m128i *)(top + x), b);
            _mm_storeu_si128((__m128i *)(bottom + x), a);
            _mm_storeu_si128((__m128i *)(top + x + 16), d);
            _mm_storeu_si128((__m128i *)(bottom + x + 16), c);
            a = _mm_loadu_si128((const __m128i *)(top + x + 32));
            b = _mm_loadu_si128((const __m128i *)(bottom + x + 32));
            c = _mm_loadu_si128((const __m128i *)(top + x + 48));
            d = _mm_loadu_si128((const __m128i *)(bottom + x + 48));
            _mm_storeu_si128((__m128i *)(top + x + 32), b);
            _mm_storeu_si128((__m128i *)(bottom + x + 32), a);
            _mm_storeu_si128((__m128i *)(top + x + 48), d);
            _mm_storeu_si128((__m128i *)(bottom + x + 48), c);
        }
        for (; row_bytes - x >= 16; x += 16) {
            __m128i a = _mm_loadu_si128((const __m128i *)(top + x));
            __m128i b = _mm_loadu_si128((const __m128i *)(bottom + x));
            _mm_storeu_si128((__m128i *)(top + x), b);
            _mm_storeu_si128((__m128i *)(bottom + x), a);
        }
        for (; x < row_bytes; x++) {
            uint8_t a = top[x];
            top[x] = bottom[x];
            bottom[x] = a;
        }
    }
}
#endif

#ifdef GLO_HAVE_AVX2_SWAP
__attribute__((target("avx2")))
static void glo_flip_rows_avx2(uint8_t *data, unsigned stride, size_t row_bytes,
                              unsigned height)
{
    if (height < 2 || !row_bytes) {
        return;
    }
    for (unsigned y = 0; y < height / 2; y++) {
        uint8_t *top = data + (size_t)y * stride;
        uint8_t *bottom = data + (size_t)(height - 1 - y) * stride;
        size_t x = 0;
        for (; row_bytes - x >= 64; x += 64) {
            __m256i a = _mm256_loadu_si256((const __m256i *)(top + x));
            __m256i b = _mm256_loadu_si256((const __m256i *)(bottom + x));
            __m256i c = _mm256_loadu_si256((const __m256i *)(top + x + 32));
            __m256i d = _mm256_loadu_si256((const __m256i *)(bottom + x + 32));
            _mm256_storeu_si256((__m256i *)(top + x), b);
            _mm256_storeu_si256((__m256i *)(bottom + x), a);
            _mm256_storeu_si256((__m256i *)(top + x + 32), d);
            _mm256_storeu_si256((__m256i *)(bottom + x + 32), c);
        }
        for (; x < row_bytes; x++) {
            uint8_t a = top[x];
            top[x] = bottom[x];
            bottom[x] = a;
        }
    }
}
#endif

static void glo_flip_rows(uint8_t *data, unsigned stride, size_t row_bytes,
                          unsigned height)
{
    /* Avoid both pointer underflow and needless allocation/probing for empty
     * or single-row images. Padding and the odd centre row stay untouched. */
    if (height < 2 || !row_bytes) {
        return;
    }
#if defined(GLO_HAVE_AVX2_SWAP) || defined(GLO_HAVE_SSE2_SWAP)
    uint64_t features = cpu_features_get();
#endif
#ifdef GLO_HAVE_AVX2_SWAP
    if (features & RETRO_SIMD_AVX2) {
        glo_flip_rows_avx2(data, stride, row_bytes, height);
        return;
    }
#endif
#ifdef GLO_HAVE_SSE2_SWAP
    if (features & RETRO_SIMD_SSE2) {
        glo_flip_rows_sse2(data, stride, row_bytes, height);
        return;
    }
#endif
    glo_flip_rows_portable(data, stride, row_bytes, height);
}

void glo_readpixels(GLenum gl_format, GLenum gl_type,
                    unsigned int bytes_per_pixel, unsigned int stride,
                    unsigned int width, unsigned int height, bool vflip,
                    void *data)
{
    /* TODO: weird strides */
    assert(stride % bytes_per_pixel == 0);

    /* Save guest processes GL state before we ReadPixels() */
    int rl, pa;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rl);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pa);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride / bytes_per_pixel);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadPixels(0, 0, width, height, gl_format, gl_type, data);

    if (vflip) {
        glo_flip_rows(data, stride, (size_t)width * bytes_per_pixel, height);
    }

    /* Restore GL state */
    glPixelStorei(GL_PACK_ROW_LENGTH, rl);
    glPixelStorei(GL_PACK_ALIGNMENT, pa);
}

bool glo_check_extension(const char* ext_name)
{
    return xemu_gl_has_extension(ext_name);
}
