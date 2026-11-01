/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef _WIN32
#include <windows.h>
#include <epoxy/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "s3tc.h"
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define g_malloc malloc
#define g_free free
typedef struct {
    bool s3tc_3d_native;
    struct {
        bool enabled;
        GLuint program, buffer;
        GLint64 max_bytes;
        GLint max_groups[3];
    } s3tc_decode;
    uint8_t *tex_scratch[2];
    size_t tex_scratch_size[2];
} PGRAPHGLState;
#include "s3tc-decode.h"
/* PRODUCTION */

static bool has_extension(const char *extension)
{
    const char *p = (const char *)glGetString(GL_EXTENSIONS);
    size_t length = strlen(extension);
    while ((p = strstr(p, extension))) {
        if ((p[length] == ' ' || !p[length]) &&
            (p == (const char *)glGetString(GL_EXTENSIONS) || p[-1] == ' ')) {
            return true;
        }
        p += length;
    }
    return false;
}

#include "bc-reference.h"

static bool force_compute_direct = true;

static void check_level(PGRAPHGLState *r, unsigned level, unsigned w,
                        unsigned h, unsigned depth, unsigned mode,
                        bool precision)
{
    unsigned bw = (w + 3) / 4, bh = (h + 3) / 4, bs = mode ? 16 : 8;
    size_t bytes = (size_t)bw * bh * depth * bs;
    uint8_t *blocks = calloc(1, bytes);
    uint8_t *expected = malloc((size_t)w * h * depth * 4);
    uint8_t *actual = malloc((size_t)w * h * depth * 4);
    const uint16_t colors[] = {0, 0xf800, 0x07e0, 0x001f,
                               0xffff, 0xffe0, 0xf81f, 0x07ff};
    assert(blocks && expected && actual);
    unsigned index = 0;
    for (unsigned z0 = 0; z0 < depth; z0 += 4)
    for (unsigned by = 0; by < bh; by++)
    for (unsigned bx = 0; bx < bw; bx++)
    for (unsigned z = 0; z < 4 && z0 + z < depth; z++) {
        unsigned code = bx + bw * (by + bh * (z0 + z));
        uint16_t rgb = colors[code % 8];
        uint8_t alpha = mode ? (code % 16) * 17 : 255;
        uint8_t *block = blocks + index++ * bs;
        unsigned offset = mode ? 8 : 0;
        block[offset] = rgb & 255;
        block[offset + 1] = rgb >> 8;
        if (mode == 1) memset(block, alpha, 8);
        if (mode == 2) block[0] = alpha;
        if (precision) {
            const uint16_t endpoints[] = {0x0841, 0x18e3, 0x39e7, 0x7bef,
                                         0xa534, 0xdead, 0x1234, 0xffff};
            uint16_t a = endpoints[code % 8];
            uint16_t b = endpoints[(code / 8) % 8];
            block[offset] = a; block[offset + 1] = a >> 8;
            block[offset + 2] = b; block[offset + 3] = b >> 8;
            memset(block + offset + 4, 0xe4, 4);
            if (mode == 1) {
                for (unsigned i = 0; i < 8; i++) block[i] = 2*i | ((2*i+1) << 4);
            } else if (mode == 2) {
                block[0] = code & 255;
                block[1] = code % 3 == 0 ? block[0] : (255 - code) & 255;
                uint64_t selectors = 0;
                for (unsigned i = 0; i < 16; i++) selectors |= (uint64_t)(i%8) << (3*i);
                for (unsigned i = 0; i < 6; i++) block[2+i] = selectors >> (8*i);
            }
        }
        for (unsigned y = by * 4; y < by * 4 + 4 && y < h; y++)
        for (unsigned x = bx * 4; x < bx * 4 + 4 && x < w; x++) {
            uint8_t *pixel = expected + (((z0 + z) * h + y) * w + x) * 4;
            pixel[0] = (rgb & 0xf800) ? 255 : 0;
            pixel[1] = (rgb & 0x07e0) ? 255 : 0;
            pixel[2] = (rgb & 31) ? 255 : 0;
            pixel[3] = alpha;
            if (precision) reference_pixel(block, mode, (y%4)*4 + x%4, pixel);
        }
    }
    GLenum formats[] = { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                         GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
                         GL_COMPRESSED_RGBA_S3TC_DXT5_EXT };
    if (r->s3tc_decode.enabled && force_compute_direct) {
        assert(decode_compressed_volume_gpu(r, level, formats[mode], blocks,
                                             w, h, depth, bytes));
    } else upload_compressed_volume(r, GL_TEXTURE_3D, level, formats[mode], blocks,
                              w, h, depth, bytes);
    assert(glGetError() == GL_NO_ERROR);
    GLint internal, width, height, layers;
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_INTERNAL_FORMAT, &internal);
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_DEPTH, &layers);
    assert((unsigned)width == w && (unsigned)height == h && (unsigned)layers == depth);
    assert((GLenum)internal == (r->s3tc_3d_native ? formats[mode] : GL_RGBA8));
    glGetTexImage(GL_TEXTURE_3D, level, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    assert(glGetError() == GL_NO_ERROR);
    for (size_t q=0;q<(size_t)w*h*depth*4;q++) if(actual[q]!=expected[q]) {
        fprintf(stderr,"Mismatch compute=%d mode=%u %ux%ux%u level=%u byte=%zu got=%u expected=%u\n",r->s3tc_decode.enabled,mode,w,h,depth,level,q,actual[q],expected[q]); abort();
    }
    if (r->s3tc_3d_native) assert(!r->tex_scratch[1]);
    free(actual);
    free(expected);
    free(blocks);
}

static void check_exhaustive_gpu(PGRAPHGLState *r)
{
    const unsigned w=1024, h=1024;
    GLenum formats[]={GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                      GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
                      GL_COMPRESSED_RGBA_S3TC_DXT5_EXT};
    uint8_t *source=malloc(65536*16), *expected=malloc(w*h*4), *actual=malloc(w*h*4);
    assert(source && expected && actual);
    for(unsigned mode=0;mode<3;mode++) for(unsigned ch=0;ch<3;ch++) {
        unsigned bs=mode?16:8, shift=ch==0?11:ch==1?5:0, mask=ch==1?63:31;
        memset(source,0,65536*16);
        for(unsigned i=0;i<65536;i++) {
            uint8_t *b=source+i*bs; unsigned off=mode?8:0;
            unsigned c0=(i&mask)<<shift, c1=((i/(mask+1))&mask)<<shift;
            b[off]=c0; b[off+1]=c0>>8; b[off+2]=c1; b[off+3]=c1>>8;
            memset(b+off+4,0xe4,4);
            if(mode==1) for(unsigned j=0;j<8;j++) b[j]=2*j|((2*j+1)<<4);
            if(mode==2) {
                b[0]=i; b[1]=i>>8;
                uint64_t selectors=0;
                for(unsigned j=0;j<16;j++) selectors|=(uint64_t)(j%8)<<(3*j);
                for(unsigned j=0;j<6;j++) b[2+j]=selectors>>(8*j);
            }
            for(unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++) {
                size_t pos=((size_t)(i/256*4+y)*w+i%256*4+x)*4;
                reference_pixel(b,mode,y*4+x,expected+pos);
            }
        }
        assert(decode_compressed_volume_gpu(r,0,formats[mode],source,w,h,1,(size_t)65536*bs));
        glGetTexImage(GL_TEXTURE_3D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        assert(glGetError()==GL_NO_ERROR);
        assert(!memcmp(actual,expected,w*h*4));
    }
    free(source);free(expected);free(actual);
    puts("compute exhaustive: 9437184 pixels, color/alpha endpoint pairs and selectors exact");
}

static void check_gpu_state(PGRAPHGLState *r)
{
    GLuint buffers[2],image,program,shader;
    GLint alignment; glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&alignment);
    glGenBuffers(2,buffers);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,alignment+64,NULL,GL_STATIC_DRAW);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,buffers[0],alignment,64);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,64,NULL,GL_STATIC_DRAW);
    glGenTextures(1,&image); glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glBindImageTexture(0,image,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA8);
    shader=glCreateShader(GL_COMPUTE_SHADER);
    const char *text="#version 430\nlayout(local_size_x=1) in; void main() {}";
    glShaderSource(shader,1,&text,NULL); glCompileShader(shader);
    program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glDeleteShader(shader);
    glUseProgram(program);
    check_level(r,2,7,5,5,2,true);
    GLint value; GLint64 range;
    glGetIntegerv(GL_CURRENT_PROGRAM,&value);assert((GLuint)value==program);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&value);assert((GLuint)value==buffers[1]);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&value);assert((GLuint)value==buffers[0]);
    glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,0,&range);assert(range==alignment);
    glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,0,&range);assert(range==64);
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME,0,&value);assert((GLuint)value==image);
    glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS,0,&value);assert(value==GL_READ_ONLY);
    glGetTexParameteriv(GL_TEXTURE_3D,GL_TEXTURE_BASE_LEVEL,&value);assert(value==0);
    glGetTexParameteriv(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,&value);assert(value==1000);
    glUseProgram(0);glDeleteProgram(program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
    glBindImageTexture(0,0,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA8);
    glDeleteBuffers(2,buffers);glDeleteTextures(1,&image);
    GLint64 limit=r->s3tc_decode.max_bytes; r->s3tc_decode.max_bytes=0;
    uint8_t source[16]={0};
    assert(!decode_compressed_volume_gpu(r,0,GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,source,4,4,1,16));
    r->s3tc_decode.max_bytes=limit;
    puts("compute state: program, ranged/indexed/generic SSBO, image and mip view restored; limits reject safely");
}

#ifndef _WIN32
#include <time.h>
#endif
static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER t,f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t);
    return (double)t.QuadPart/f.QuadPart;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec*1e-9;
#endif
}

static void benchmark(bool vtc)
{
    unsigned widths[] = {16,32,64,128,256};
    GLenum formats[] = {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                         GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
                         GL_COMPRESSED_RGBA_S3TC_DXT5_EXT};
    for (unsigned wi=0;wi<5;wi++) for(unsigned mode=0;mode<3;mode++) {
        unsigned w=widths[wi], h=w, d=8, bs=mode?16:8;
        size_t bytes=(size_t)((w+3)/4)*((h+3)/4)*d*bs;
        uint8_t *source=malloc(bytes); assert(source);
        for(size_t j=0;j<bytes;j++) source[j]=(j*31+j/17)&255;
        PGRAPHGLState paths[3]={0}; GLuint tex[3]; glGenTextures(3,tex);
        paths[1].s3tc_decode.enabled=true;
        paths[1].s3tc_decode.max_bytes=1<<27;
        for(unsigned j=0;j<3;j++) paths[1].s3tc_decode.max_groups[j]=65535;
        paths[2].s3tc_3d_native=true;
        double elapsed[3]={0}; unsigned n=20;
        for(unsigned round=0;round<9;round++) for(unsigned k=0;k<2u+(unsigned)vtc;k++) {
            unsigned path=(k+round)%(2u+(unsigned)vtc);
            glBindTexture(GL_TEXTURE_3D,tex[path]);
            glFinish(); double begin=seconds();
            for(unsigned j=0;j<n;j++) {
                if(path==1) assert(decode_compressed_volume_gpu(&paths[path],0,formats[mode],source,w,h,d,bytes));
                else upload_compressed_volume(&paths[path],GL_TEXTURE_3D,0,formats[mode],source,w,h,d,bytes);
            }
            glFinish(); double end=seconds();
            assert(glGetError()==GL_NO_ERROR);
            if(round) elapsed[path]+=end-begin;
        }
        printf("bench width=%u depth=%u format=%u cpu_us=%.3f compute_us=%.3f native_us=%.3f\n",w,d,mode,
            elapsed[0]*1e6/(8*n),elapsed[1]*1e6/(8*n),vtc?elapsed[2]*1e6/(8*n):0.0);
        for(unsigned path=0;path<3;path++) {
            glDeleteProgram(paths[path].s3tc_decode.program);
            glDeleteBuffers(1,&paths[path].s3tc_decode.buffer);
            free(paths[path].tex_scratch[1]);
        }
        glDeleteTextures(3,tex); free(source);
    }
}

int main(void)
{
#ifdef _WIN32
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "vtc-oracle";
    assert(RegisterClassA(&wc));
    HWND hwnd = CreateWindowA(wc.lpszClassName, "vtc", WS_OVERLAPPEDWINDOW,
                             0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    assert(hwnd);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
    assert(SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd));
    HGLRC context = wglCreateContext(dc);
    assert(context && wglMakeCurrent(dc, context));
#else
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(eglInitialize(display, NULL, NULL));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                       EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config;
    EGLint count;
    assert(eglChooseConfig(display, attrs, &config, 1, &count) && count);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    assert(context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
#endif
    bool vtc = has_extension("GL_NV_texture_compression_vtc");
    printf("GL volume oracle: %s; %s; VTC=%d S3TC=%d\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION), vtc,
           has_extension("GL_EXT_texture_compression_s3tc"));
    unsigned cases = 0;
    for (unsigned path = 0; path < 2u + (unsigned)vtc; path++) {
        unsigned native = path == 2;
        bool gpu = path == 1;
        GLint major, minor;
        glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (gpu && major*10+minor < 43) continue;
        PGRAPHGLState r = { .s3tc_3d_native = native };
        r.s3tc_decode.enabled = gpu;
        if (gpu) {
            glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &r.s3tc_decode.max_bytes);
            for (unsigned i=0; i<3; i++) glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &r.s3tc_decode.max_groups[i]);
        }
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_3D, texture);
        if (gpu && getenv("GL_VOLUME_BAD_SHADER")) {
            force_compute_direct=false;
            check_level(&r,0,64,64,8,2,true);
            assert(!r.s3tc_decode.enabled && !r.s3tc_decode.program && !r.s3tc_decode.buffer);
            assert(r.tex_scratch[1]);
            glDeleteTextures(1,&texture); free(r.tex_scratch[1]);
            force_compute_direct=true;
            puts("compute compile failure: CPU fallback exact, failed resources released");
            continue;
        }
        for (unsigned mode = 0; mode < 3; mode++) {
            for (unsigned d = 1; d <= 8; d *= 2) {
                check_level(&r, 0, 8, 8, d, mode, false); cases++;
            }
            for (unsigned level = 0; level < 4; level++) {
                check_level(&r, level, 16 >> level, 8 >> level,
                            MAX(8 >> level, 1u), mode, false); cases++;
            }
        }
        if (!native) {
            for (unsigned mode = 0; mode < 3; mode++) {
                check_level(&r, 0, 32, 32, 8, mode, true); cases++;
                check_level(&r, 0, 7, 5, 3, mode, true); cases++;
                for (unsigned level = 0; level < 5; level++) {
                    check_level(&r, level, MAX(16 >> level, 1u),
                                MAX(8 >> level, 1u), MAX(8 >> level, 1u),
                                mode, true); cases++;
                }
            }
            puts("fallback: 21 interpolated RGB/alpha levels exact against normalized oracle");
        }
        printf("native=%u: 24 volume levels exact, alpha and block/slice order preserved\n",
               native);
        if (gpu) {
            check_exhaustive_gpu(&r);
            check_gpu_state(&r);
            assert(!r.tex_scratch[1]);
            force_compute_direct=false;
            check_level(&r,0,64,64,8,2,true);
            assert(!r.tex_scratch[1]); /* Actual upload caller selected compute. */
            check_level(&r,0,4,4,1,2,true);
            assert(r.tex_scratch[1]); /* Small upload selected CPU. */
            force_compute_direct=true;
            cases+=3;
        }
        printf("compute=%d program=%u\n", gpu, r.s3tc_decode.program);
        glDeleteProgram(r.s3tc_decode.program);
        glDeleteBuffers(1, &r.s3tc_decode.buffer);
        glDeleteTextures(1, &texture);
        free(r.tex_scratch[1]);
    }
    printf("volume cases passed=%u\n", cases);
    if (getenv("GL_VOLUME_BENCH")) benchmark(vtc);
#ifdef _WIN32
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(context);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
#else
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
#endif
    return 0;
}
