/* libepoxy as the Makefile builds it: WGL on Windows, EGL on Android */
#ifdef __ANDROID__
#define ENABLE_EGL 1
#else
#define ENABLE_EGL 0
#endif
#define ENABLE_GLX 0
#define ENABLE_X11 0
