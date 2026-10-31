/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GL_DISPATCH_H
#define XEMU_GL_DISPATCH_H

#include <stdbool.h>
#ifdef _WIN32
#include <glsym/glsym.h>

/* Extensions used by the renderer but absent from common's generated table. */
extern PFNGLCREATEMEMORYOBJECTSEXTPROC xemu_glCreateMemoryObjectsEXT;
extern PFNGLDELETEMEMORYOBJECTSEXTPROC xemu_glDeleteMemoryObjectsEXT;
extern PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC xemu_glImportMemoryWin32HandleEXT;
extern PFNGLTEXSTORAGEMEM2DEXTPROC xemu_glTexStorageMem2DEXT;
extern PFNGLMAXSHADERCOMPILERTHREADSARBPROC xemu_glMaxShaderCompilerThreadsARB;
extern PFNGLFRAMETERMINATORGREMEDYPROC xemu_glFrameTerminatorGREMEDY;
#define glCreateMemoryObjectsEXT xemu_glCreateMemoryObjectsEXT
#define glDeleteMemoryObjectsEXT xemu_glDeleteMemoryObjectsEXT
#define glImportMemoryWin32HandleEXT xemu_glImportMemoryWin32HandleEXT
#define glTexStorageMem2DEXT xemu_glTexStorageMem2DEXT
#define glMaxShaderCompilerThreadsARB xemu_glMaxShaderCompilerThreadsARB
#define glFrameTerminatorGREMEDY xemu_glFrameTerminatorGREMEDY

/* Called only during frontend context setup, before publishing to workers. */
bool xemu_gl_load(rglgen_proc_address_t resolve);
int xemu_gl_version(void);
bool xemu_gl_has_extension(const char *name);
#else
#include <epoxy/gl.h>
#define xemu_gl_version epoxy_gl_version
#define xemu_gl_has_extension epoxy_has_gl_extension
#endif
#endif
