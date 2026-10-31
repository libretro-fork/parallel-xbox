/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ui/gl-dispatch.h"
#include <glsym/rglgen_private_headers.h>
#include <string.h>

PFNGLCREATEMEMORYOBJECTSEXTPROC xemu_glCreateMemoryObjectsEXT;
PFNGLDELETEMEMORYOBJECTSEXTPROC xemu_glDeleteMemoryObjectsEXT;
PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC xemu_glImportMemoryWin32HandleEXT;
PFNGLTEXSTORAGEMEM2DEXTPROC xemu_glTexStorageMem2DEXT;
PFNGLMAXSHADERCOMPILERTHREADSARBPROC xemu_glMaxShaderCompilerThreadsARB;
PFNGLFRAMETERMINATORGREMEDYPROC xemu_glFrameTerminatorGREMEDY;

/* Context creation and this flag belong to the frontend thread. Once workers
 * start the dispatch table is immutable, including on subsequent resets. The
 * worker contexts share the same adapter/pixel format as the frontend's. */
static bool loaded;
static rglgen_proc_address_t frontend_resolve;

static rglgen_func_t resolve_proc(const char *name)
{
    rglgen_func_t proc = frontend_resolve ? frontend_resolve(name) : NULL;
    if (!proc) {
        PROC native = wglGetProcAddress(name);
        /* Some WGL drivers return failure sentinels instead of NULL. */
        if (native && (uintptr_t)native > 3 && (intptr_t)native != -1) {
            proc = (rglgen_func_t)native;
        }
    }
    return proc;
}

bool xemu_gl_load(rglgen_proc_address_t resolve)
{
    if (loaded) {
        return true;
    }
    if (!glGetString(GL_VERSION)) {
        return false;
    }
    frontend_resolve = resolve;
    rglgen_resolve_symbols(resolve_proc);
#define LOAD(name) do { \
        rglgen_func_t proc = resolve_proc(#name); \
        memcpy(&name, &proc, sizeof(name)); \
    } while (0)
    LOAD(glCreateMemoryObjectsEXT);
    LOAD(glDeleteMemoryObjectsEXT);
    LOAD(glImportMemoryWin32HandleEXT);
    LOAD(glTexStorageMem2DEXT);
    LOAD(glMaxShaderCompilerThreadsARB);
    LOAD(glFrameTerminatorGREMEDY);
#undef LOAD
    if (!glGetStringi || !glCreateShader || !glGenFramebuffers ||
        !glFenceSync || !glBindVertexArray) {
        return false;
    }
    loaded = true;
    return true;
}

int xemu_gl_version(void)
{
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    return major * 10 + minor;
}

bool xemu_gl_has_extension(const char *name)
{
    GLint count = 0;
    if (!name || !*name || strchr(name, ' ')) {
        return false;
    }
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, i);
        if (extension && !strcmp(extension, name)) {
            return true;
        }
    }
    return false;
}
