/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void) {
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
 assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
 EGLConfig config;EGLint count;assert(eglChooseConfig(display,attrs,&config,1,&count)&&count==1);
 EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);assert(context!=EGL_NO_CONTEXT);
 assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
 GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);
 const char *source="#version 430\nlayout(location=0)out vec4 color;void main(){color=vec4(0.25,0.5,0.75,1.0);}\n";
 glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
 GLint good;glGetShaderiv(shader,GL_COMPILE_STATUS,&good);assert(good);
 GLuint program=glCreateProgram();glProgramParameteri(program,GL_PROGRAM_SEPARABLE,GL_TRUE);
 glProgramParameteri(program,GL_PROGRAM_BINARY_RETRIEVABLE_HINT,GL_TRUE);
 glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&good);assert(good);
 glGetProgramiv(program,GL_PROGRAM_BINARY_LENGTH,&query_size);assert(query_size>0);
 expected_binary=malloc(query_size);assert(expected_binary);
 glGetProgramBinary(program,query_size,&copied_size,&expected_format,expected_binary);assert(glGetError()==GL_NO_ERROR);
 ModuleDiskSave metadata;
 GLDiskWriter w={.slot={.data=&metadata,.base_path="fixture",.vendor=shader_gl_vendor}};
 PGRAPHGLState r={.cache_writer=&w};ShaderModuleCacheEntry module={.gl_stage_program=program};
 for(unsigned i=0;i<12;i++)module.key.words[i]=i*17;
 assert(module_save_to_disk(&r,&module,17));check_blob(&module);
 free(w.slot.scratch);free(expected_binary);glDeleteProgram(program);glDeleteShader(shader);
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 eglDestroyContext(display,context);eglTerminate(display);
 puts("PASS: Mesa GL program exported through production capture/serializer; format, key and binary bytes match direct driver oracle");
}
