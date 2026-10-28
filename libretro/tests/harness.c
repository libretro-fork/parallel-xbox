/* Headless libretro frontend for measuring the core: GL core context on a
 * surfaceless EGL display (Mesa) - on Windows a WGL context on a hidden
 * window, which Wine runs on the host's GL - one FBO, unthrottled
 * retro_run.
 *
 * Per run it records the audio frames handed over and whether a new video
 * frame came; at the end it prints the cadence, the speed and hashes of the
 * audio and of the frames (with --hash-video), and can write the audio to a
 * WAV file.
 *
 *   harness <core.so> <system dir> [content] [options]
 *     --frames N        runs to do (default 600)
 *     --wav FILE        write the audio
 *     --hash-video      read back every frame and hash it
 *     --opt key=value   a core option
 *     --realtime        pace runs at 59.94 Hz instead of running flat out
 *     --rt-runs N       pace only the first N runs, then flat out
 *     --key F:ID[:LEN]  hold RETROK id for LEN runs from run F
 *     --pad F:ID[:LEN]  hold joypad id on port 0 for LEN runs from run F
 *     --csv FILE        per-run log: run, audio frames, video flag, us
 *     --save N          retro_serialize before run N
 *     --load N          retro_unserialize (the state saved) before run N
 *     --state-out FILE  also write the state saved (--save) to FILE, and
 *                       the system RAM at that point to FILE.ram
 *     --state-in FILE   the state --load loads is FILE's (and with
 *                       --verify-load, the RAM to compare is FILE.ram)
 *     --verify-load     exit 1 unless the system RAM right after the load
 *                       is the RAM as it was when the state was saved
 *     --reset N         retro_reset before run N
 *     --reload N        close the content and load it again before run N
 *     --strict          exit 1 unless every run gave one frame's worth of
 *                       audio in one batch and a video call
 *     --min-speed X     exit 1 if the unpaced runs ran slower than X times
 *                       real time
 *
 * Plain C99. SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif
#include <GL/gl.h>
#include <GL/glext.h>

#include "../../ui/libretro.h"

#define MAX_OPTS 64
#define MAX_EVENTS 64

static struct { char key[128]; char val[128]; } opts[MAX_OPTS];
static int nopts;
static int opts_dirty;

typedef struct { int first, id, len, is_key; } ev_t;
static ev_t evs[MAX_EVENTS];
static int nevs;

static const char *sysdir;
#ifdef _WIN32
static HWND hwnd;
static HDC hdc;
static HGLRC hglrc;
#else
static EGLDisplay dpy;
static EGLContext ctx;
#endif
static GLuint fbo, fbo_color, fbo_depth;
static struct retro_hw_render_callback hw;
static int have_hw;
static retro_keyboard_event_t kb_cb;

static int run_no;
static long run_audio;
static int run_video, run_video_dupe;
static FILE *wav;
static long wav_frames;
static uint64_t audio_hash = 1469598103934665603ull;
static uint64_t video_hash = 1469598103934665603ull;
static int hash_video;
static unsigned last_w, last_h;
static int batches_this_run;

static PFNGLGENFRAMEBUFFERSPROC p_glGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC p_glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_glFramebufferTexture2D;
static PFNGLGENRENDERBUFFERSPROC p_glGenRenderbuffers;
static PFNGLBINDRENDERBUFFERPROC p_glBindRenderbuffer;
static PFNGLRENDERBUFFERSTORAGEPROC p_glRenderbufferStorage;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC p_glFramebufferRenderbuffer;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
   const unsigned char *b = (const unsigned char *)p;
   size_t i;
   for (i = 0; i < n; i++)
   {
      h ^= b[i];
      h *= 1099511628211ull;
   }
   return h;
}

static int64_t now_us(void)
{
#ifdef _WIN32
   static LARGE_INTEGER freq;
   LARGE_INTEGER t;
   if (!freq.QuadPart)
      QueryPerformanceFrequency(&freq);
   QueryPerformanceCounter(&t);
   return (int64_t)(t.QuadPart / freq.QuadPart) * 1000000 +
      (int64_t)(t.QuadPart % freq.QuadPart) * 1000000 / freq.QuadPart;
#else
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
   if (level == RETRO_LOG_DEBUG && !getenv("HARNESS_DEBUG"))
      return;
   fprintf(stderr, "[core %s] ", names[level & 3]);
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static uintptr_t get_fb(void) { return fbo; }

static retro_proc_address_t get_proc(const char *sym)
{
#ifdef _WIN32
   /* wglGetProcAddress has only what is past GL 1.1; opengl32 exports the
    * rest, and some drivers answer 1, 2, 3 or -1 for those */
   PROC p = wglGetProcAddress(sym);
   if (!p || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 || p == (PROC)-1)
      p = GetProcAddress(GetModuleHandleA("opengl32.dll"), sym);
   return (retro_proc_address_t)p;
#else
   return (retro_proc_address_t)eglGetProcAddress(sym);
#endif
}

static bool set_rumble(unsigned port, enum retro_rumble_effect e, uint16_t s)
{
   (void)port; (void)e; (void)s;
   return 1;
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb;
         return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = sysdir;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;
      case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
         *(unsigned *)data = RETRO_HW_CONTEXT_OPENGL_CORE;
         return true;
      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      {
         struct retro_hw_render_callback *cb =
            (struct retro_hw_render_callback *)data;
         if (cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE)
            return false;
         hw = *cb;
         cb->get_current_framebuffer = get_fb;
         cb->get_proc_address = get_proc;
         hw.get_current_framebuffer = get_fb;
         hw.get_proc_address = get_proc;
         have_hw = 1;
         return true;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *v = (struct retro_variable *)data;
         int i;
         for (i = 0; i < nopts; i++)
            if (!strcmp(opts[i].key, v->key))
            {
               v->value = opts[i].val;
               return true;
            }
         v->value = NULL;
         return false;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = opts_dirty;
         opts_dirty = 0;
         return true;
      case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
         ((struct retro_rumble_interface *)data)->set_rumble_state = set_rumble;
         return true;
      case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
         kb_cb = ((struct retro_keyboard_callback *)data)->callback;
         return true;
      case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
         *(unsigned *)data = 1;
         return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
         fprintf(stderr, "[msg] %s\n",
               ((const struct retro_message_ext *)data)->msg);
         return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE:
         fprintf(stderr, "[msg] %s\n",
               ((const struct retro_message *)data)->msg);
         return true;
      case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
         *(bool *)data = false;
         return true;
      default:
         return false;
   }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
   (void)pitch;
   if (!data)
   {
      run_video_dupe++;
      return;
   }
   run_video++;
   last_w = w;
   last_h = h;
   if (hash_video && data == RETRO_HW_FRAME_BUFFER_VALID)
   {
      static unsigned char *px;
      static size_t cap;
      size_t need = (size_t)w * h * 4;
      if (need > cap)
      {
         px = (unsigned char *)realloc(px, need);
         cap = need;
      }
      p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
      p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
      video_hash = fnv(video_hash, px, need);
   }
}

static void audio_cb(int16_t l, int16_t r)
{
   int16_t s[2];
   s[0] = l;
   s[1] = r;
   run_audio++;
   audio_hash = fnv(audio_hash, s, sizeof(s));
   if (wav)
   {
      fwrite(s, sizeof(s), 1, wav);
      wav_frames++;
   }
}

static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
   run_audio += (long)frames;
   batches_this_run++;
   audio_hash = fnv(audio_hash, d, frames * 4);
   if (wav)
   {
      fwrite(d, 4, frames, wav);
      wav_frames += (long)frames;
   }
   return frames;
}

static int held(int is_key, unsigned id)
{
   int i;
   for (i = 0; i < nevs; i++)
      if (evs[i].is_key == is_key && (unsigned)evs[i].id == id &&
          run_no >= evs[i].first && run_no < evs[i].first + evs[i].len)
         return 1;
   return 0;
}

static void input_poll_cb(void)
{
   int i;
   if (!kb_cb)
      return;
   for (i = 0; i < nevs; i++)
   {
      if (!evs[i].is_key)
         continue;
      if (run_no == evs[i].first)
         kb_cb(true, (unsigned)evs[i].id, 0, 0);
      else if (run_no == evs[i].first + evs[i].len)
         kb_cb(false, (unsigned)evs[i].id, 0, 0);
   }
}

static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx,
      unsigned id)
{
   (void)idx;
   if (dev == RETRO_DEVICE_KEYBOARD)
      return (int16_t)held(1, id);
   if (port == 0 && dev == RETRO_DEVICE_JOYPAD)
   {
      if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
      {
         int16_t m = 0;
         unsigned b;
         for (b = 0; b < 16; b++)
            if (held(0, b))
               m |= (int16_t)(1 << b);
         return m;
      }
      return (int16_t)held(0, id);
   }
   return 0;
}

/* The frame buffer the core draws into, on the context gl_setup made */
static void fbo_setup(void)
{
   p_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)get_proc("glGenFramebuffers");
   p_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)get_proc("glBindFramebuffer");
   p_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)get_proc("glFramebufferTexture2D");
   p_glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)get_proc("glGenRenderbuffers");
   p_glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)get_proc("glBindRenderbuffer");
   p_glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)get_proc("glRenderbufferStorage");
   p_glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)get_proc("glFramebufferRenderbuffer");

   glGenTextures(1, &fbo_color);
   glBindTexture(GL_TEXTURE_2D, fbo_color);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, GL_RGBA,
         GL_UNSIGNED_BYTE, NULL);
   p_glGenRenderbuffers(1, &fbo_depth);
   p_glBindRenderbuffer(GL_RENDERBUFFER, fbo_depth);
   p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1920, 1080);
   p_glGenFramebuffers(1, &fbo);
   p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
         GL_TEXTURE_2D, fbo_color, 0);
   p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
         GL_RENDERBUFFER, fbo_depth);
   p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#ifdef _WIN32
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
typedef HGLRC (WINAPI *create_attribs_t)(HDC, HGLRC, const int *);

static int gl_setup(void)
{
   static const int attr[] = {
      WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
      WGL_CONTEXT_MINOR_VERSION_ARB, 5,
      WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
      0
   };
   PIXELFORMATDESCRIPTOR pfd;
   WNDCLASSA wc;
   HGLRC tmp;
   create_attribs_t create;

   memset(&wc, 0, sizeof(wc));
   wc.lpfnWndProc = DefWindowProcA;
   wc.hInstance = GetModuleHandleA(NULL);
   wc.lpszClassName = "harness";
   RegisterClassA(&wc);
   hwnd = CreateWindowA("harness", "harness", WS_OVERLAPPEDWINDOW, 0, 0, 64,
         64, NULL, NULL, wc.hInstance, NULL);
   if (!hwnd)
      return 0;
   hdc = GetDC(hwnd);
   memset(&pfd, 0, sizeof(pfd));
   pfd.nSize = sizeof(pfd);
   pfd.nVersion = 1;
   pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
   pfd.iPixelType = PFD_TYPE_RGBA;
   pfd.cColorBits = 32;
   pfd.cDepthBits = 24;
   pfd.cStencilBits = 8;
   if (!SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd))
      return 0;
   tmp = wglCreateContext(hdc);
   if (!tmp || !wglMakeCurrent(hdc, tmp))
      return 0;
   create = (create_attribs_t)(void *)wglGetProcAddress(
         "wglCreateContextAttribsARB");
   hglrc = create ? create(hdc, NULL, attr) : NULL;
   wglMakeCurrent(NULL, NULL);
   wglDeleteContext(tmp);
   return hglrc && wglMakeCurrent(hdc, hglrc);
}
#else
static int gl_setup(void)
{
   static const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
      EGL_NONE
   };
   static const EGLint ctx_attr[] = {
      EGL_CONTEXT_MAJOR_VERSION, 4,
      EGL_CONTEXT_MINOR_VERSION, 5,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
      EGL_NONE
   };
   PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
      eglGetProcAddress("eglGetPlatformDisplayEXT");
   EGLConfig cfg;
   EGLint n = 0;

   dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (!eglInitialize(dpy, NULL, NULL))
      return 0;
   eglBindAPI(EGL_OPENGL_API);
   if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || !n)
   {
      /* surfaceless displays may expose configs without pbuffer support */
      static const EGLint cfg2[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
         EGL_NONE };
      if (!eglChooseConfig(dpy, cfg2, &cfg, 1, &n) || !n)
         return 0;
   }
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
   if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
      return 0;

   return 1;
}
#endif

static void wav_header(FILE *f, long frames)
{
   uint32_t u;
   uint16_t s;
   fseek(f, 0, SEEK_SET);
   fwrite("RIFF", 1, 4, f);
   u = (uint32_t)(36 + frames * 4); fwrite(&u, 4, 1, f);
   fwrite("WAVEfmt ", 1, 8, f);
   u = 16; fwrite(&u, 4, 1, f);
   s = 1; fwrite(&s, 2, 1, f);
   s = 2; fwrite(&s, 2, 1, f);
   u = 48000; fwrite(&u, 4, 1, f);
   u = 48000 * 4; fwrite(&u, 4, 1, f);
   s = 4; fwrite(&s, 2, 1, f);
   s = 16; fwrite(&s, 2, 1, f);
   fwrite("data", 1, 4, f);
   u = (uint32_t)(frames * 4); fwrite(&u, 4, 1, f);
}

static int write_file(const char *path, const void *data, size_t size)
{
   FILE *f = fopen(path, "wb");
   int ok = f && fwrite(data, 1, size, f) == size;
   if (f && fclose(f))
      ok = 0;
   if (!ok)
      fprintf(stderr, "harness: cannot write %s\n", path);
   return ok;
}

static void *read_file(const char *path, size_t *size)
{
   FILE *f = fopen(path, "rb");
   void *data = NULL;
   long n;
   if (!f)
   {
      fprintf(stderr, "harness: cannot read %s\n", path);
      return NULL;
   }
   if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 &&
       fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)n)) &&
       fread(data, 1, (size_t)n, f) == (size_t)n)
      *size = (size_t)n;
   else
   {
      free(data);
      data = NULL;
   }
   fclose(f);
   return data;
}

int main(int argc, char **argv)
{
   void *core;
   int frames = 600, realtime = 0, rt_runs = -1, i;
   int save_at = -1, load_at = -1, reset_at = -1, reload_at = -1;
   int strict = 0, fail = 0;
   double min_speed = 0.0;
   void *state = NULL;
   size_t state_size = 0;
   const char *state_out = NULL, *state_in = NULL;
   int verify_load = 0;
   unsigned char *saved_ram = NULL;
   size_t saved_ram_size = 0;
   size_t (*p_ser_size)(void);
   bool (*p_ser)(void *, size_t);
   bool (*p_unser)(const void *, size_t);
   void (*p_reset)(void);
   const char *content = NULL, *wavpath = NULL, *csvpath = NULL;
   FILE *csv = NULL;
   struct retro_game_info gi;
   struct retro_system_av_info av;
   long audio_total = 0, audio_min = 1L << 30, audio_max = 0;
   int video_new = 0, video_dupe = 0, runs_without_video = 0;
   int multi_batch_runs = 0;
   int64_t t0, t_load, run_max = 0, t_free = 0;
   long expected_total = 0;
   double audio_frac = 0.0;
   long cadence_errors = 0;

   void (*p_set_env)(retro_environment_t);
   void (*p_set_video)(retro_video_refresh_t);
   void (*p_set_audio)(retro_audio_sample_t);
   void (*p_set_batch)(retro_audio_sample_batch_t);
   void (*p_set_poll)(retro_input_poll_t);
   void (*p_set_state)(retro_input_state_t);
   void (*p_init)(void);
   bool (*p_load)(const struct retro_game_info *);
   void (*p_av)(struct retro_system_av_info *);
   void (*p_run)(void);
   void (*p_unload)(void);
   void (*p_deinit)(void);
   void *(*p_mem_data)(unsigned);
   size_t (*p_mem_size)(unsigned);

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s core system [content] [options]\n", argv[0]);
      return 2;
   }
   sysdir = argv[2];
   for (i = 3; i < argc; i++)
   {
      if (!strcmp(argv[i], "--frames") && i + 1 < argc)
         frames = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
         wavpath = argv[++i];
      else if (!strcmp(argv[i], "--csv") && i + 1 < argc)
         csvpath = argv[++i];
      else if (!strcmp(argv[i], "--hash-video"))
         hash_video = 1;
      else if (!strcmp(argv[i], "--realtime"))
         realtime = 1;
      else if (!strcmp(argv[i], "--save") && i + 1 < argc)
         save_at = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--load") && i + 1 < argc)
         load_at = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--strict"))
         strict = 1;
      else if (!strcmp(argv[i], "--state-out") && i + 1 < argc)
         state_out = argv[++i];
      else if (!strcmp(argv[i], "--state-in") && i + 1 < argc)
         state_in = argv[++i];
      else if (!strcmp(argv[i], "--verify-load"))
         verify_load = 1;
      else if (!strcmp(argv[i], "--min-speed") && i + 1 < argc)
         min_speed = atof(argv[++i]);
      else if (!strcmp(argv[i], "--reload") && i + 1 < argc)
         reload_at = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--reset") && i + 1 < argc)
         reset_at = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--rt-runs") && i + 1 < argc)
         rt_runs = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--opt") && i + 1 < argc && nopts < MAX_OPTS)
      {
         const char *kv = argv[++i];
         const char *eq = strchr(kv, '=');
         if (eq)
         {
            snprintf(opts[nopts].key, sizeof(opts[nopts].key), "%.*s",
                  (int)(eq - kv), kv);
            snprintf(opts[nopts].val, sizeof(opts[nopts].val), "%s", eq + 1);
            nopts++;
         }
      }
      else if ((!strcmp(argv[i], "--key") || !strcmp(argv[i], "--pad")) &&
            i + 1 < argc && nevs < MAX_EVENTS)
      {
         ev_t *e = &evs[nevs++];
         e->is_key = argv[i][2] == 'k';
         e->len = 1;
         sscanf(argv[++i], "%d:%d:%d", &e->first, &e->id, &e->len);
      }
      else if (argv[i][0] != '-')
         content = argv[i];
   }

   if (!gl_setup())
   {
      fprintf(stderr, "harness: no GL 4.5 core context\n");
      return 1;
   }
   fbo_setup();

#ifdef _WIN32
   core = (void *)LoadLibraryA(argv[1]);
#else
   core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
#endif
   if (!core)
   {
#ifdef _WIN32
      fprintf(stderr, "harness: cannot load %s (error %lu)\n", argv[1],
            (unsigned long)GetLastError());
#else
      fprintf(stderr, "harness: %s\n", dlerror());
#endif
      return 1;
   }
#ifdef _WIN32
#define SYM(v, n) *(void **)&v = (void *)GetProcAddress((HMODULE)core, n)
#else
#define SYM(v, n) *(void **)&v = dlsym(core, n)
#endif
   SYM(p_set_env, "retro_set_environment");
   SYM(p_set_video, "retro_set_video_refresh");
   SYM(p_set_audio, "retro_set_audio_sample");
   SYM(p_set_batch, "retro_set_audio_sample_batch");
   SYM(p_set_poll, "retro_set_input_poll");
   SYM(p_set_state, "retro_set_input_state");
   SYM(p_init, "retro_init");
   SYM(p_load, "retro_load_game");
   SYM(p_av, "retro_get_system_av_info");
   SYM(p_run, "retro_run");
   SYM(p_unload, "retro_unload_game");
   SYM(p_deinit, "retro_deinit");
   SYM(p_mem_data, "retro_get_memory_data");
   SYM(p_mem_size, "retro_get_memory_size");
   SYM(p_ser_size, "retro_serialize_size");
   SYM(p_ser, "retro_serialize");
   SYM(p_unser, "retro_unserialize");
   SYM(p_reset, "retro_reset");

   p_set_env(env_cb);
   p_set_video(video_cb);
   p_set_audio(audio_cb);
   p_set_batch(audio_batch_cb);
   p_set_poll(input_poll_cb);
   p_set_state(input_state_cb);
   p_init();

   memset(&gi, 0, sizeof(gi));
   gi.path = content;
   t0 = now_us();
   if (!p_load(content ? &gi : NULL))
   {
      fprintf(stderr, "harness: retro_load_game failed\n");
      return 1;
   }
   p_av(&av);
   if (have_hw && hw.context_reset)
      hw.context_reset();
   t_load = now_us() - t0;

   if (wavpath)
   {
      wav = fopen(wavpath, "wb");
      if (wav)
         wav_header(wav, 0);
   }
   if (csvpath)
      csv = fopen(csvpath, "w");

   t0 = now_us();
   for (run_no = 0; run_no < frames; run_no++)
   {
      int64_t a, d;
      if (run_no == rt_runs)
      {
         t_free = now_us();
         run_max = 0;
      }
      if (run_no == save_at)
      {
         int64_t t = now_us();
         int ok;
         state_size = p_ser_size();
         state = malloc(state_size ? state_size : 1);
         ok = state && p_ser(state, state_size);
         printf("save at %d: size=%zu ok=%d ms=%.1f\n", run_no, state_size,
               ok, (now_us() - t) / 1000.0);
         if (!ok)
            fail = 1;
         saved_ram_size = p_mem_size(RETRO_MEMORY_SYSTEM_RAM);
         if (saved_ram_size && (saved_ram = malloc(saved_ram_size)))
            memcpy(saved_ram, p_mem_data(RETRO_MEMORY_SYSTEM_RAM),
                  saved_ram_size);
         if (ok && state_out)
         {
            char path[4096];
            snprintf(path, sizeof(path), "%s.ram", state_out);
            if (!write_file(state_out, state, state_size) ||
                (saved_ram && !write_file(path, saved_ram, saved_ram_size)))
               fail = 1;
         }
      }
      if (run_no == load_at && state_in)
      {
         char path[4096];
         free(state);
         free(saved_ram);
         state = read_file(state_in, &state_size);
         snprintf(path, sizeof(path), "%s.ram", state_in);
         saved_ram = read_file(path, &saved_ram_size);
      }
      if (run_no == load_at && state)
      {
         int64_t t = now_us();
         int ok = p_unser(state, state_size);
         printf("load at %d: ok=%d ms=%.1f\n", run_no, ok,
               (now_us() - t) / 1000.0);
         if (!ok)
            fail = 1;
         if (verify_load)
         {
            size_t n = p_mem_size(RETRO_MEMORY_SYSTEM_RAM);
            const unsigned char *ram = (const unsigned char *)
               p_mem_data(RETRO_MEMORY_SYSTEM_RAM);
            size_t diff = 0, k;
            if (!saved_ram || !ram || n != saved_ram_size)
               diff = (size_t)-1;
            else
               for (k = 0; k < n; k++)
                  diff += ram[k] != saved_ram[k];
            printf("load at %d: ram_bytes_differing=%zu\n", run_no, diff);
            if (diff)
            {
               printf("FAIL: the RAM after the load is not the RAM the "
                     "state was saved with\n");
               fail = 1;
            }
         }
      }
      if (run_no == reload_at)
      {
         int64_t t = now_us();
         if (hw.context_destroy)
            hw.context_destroy();
         p_unload();
         p_deinit();
         have_hw = 0;
         p_set_env(env_cb);
         p_init();
         if (!p_load(content ? &gi : NULL))
         {
            fprintf(stderr, "harness: reload failed\n");
            return 1;
         }
         if (have_hw && hw.context_reset)
            hw.context_reset();
         printf("reload at %d: ms=%.1f\n", run_no, (now_us() - t) / 1000.0);
      }
      if (run_no == reset_at)
      {
         p_reset();
         printf("reset at %d\n", run_no);
      }
      a = now_us();
      run_audio = 0;
      run_video = 0;
      run_video_dupe = 0;
      batches_this_run = 0;
      p_run();
      d = now_us() - a;
      if (d > run_max)
         run_max = d;
      audio_total += run_audio;
      if (run_audio < audio_min)
         audio_min = run_audio;
      if (run_audio > audio_max)
         audio_max = run_audio;
      if (batches_this_run > 1)
         multi_batch_runs++;
      audio_frac += av.timing.sample_rate / av.timing.fps;
      {
         long want = (long)audio_frac;
         audio_frac -= (double)want;
         expected_total += want;
         if (run_audio != want)
            cadence_errors++;
      }
      video_new += run_video;
      video_dupe += run_video_dupe;
      if (!run_video && !run_video_dupe)
         runs_without_video++;
      if (csv)
         fprintf(csv, "%d,%ld,%d,%lld\n", run_no, run_audio,
               run_video ? 1 : (run_video_dupe ? 2 : 0), (long long)d);
      if (realtime || run_no < rt_runs)
      {
         int64_t target = t0 + (int64_t)((run_no + 1) * 1001e6 / 60000.0);
         int64_t left = target - now_us();
         if (left > 0)
         {
#ifdef _WIN32
            Sleep((DWORD)(left / 1000));
#else
            struct timespec ts;
            ts.tv_sec = left / 1000000;
            ts.tv_nsec = (left % 1000000) * 1000;
            nanosleep(&ts, NULL);
#endif
         }
      }
   }
   {
      int64_t el = now_us() - t0;
      int fl = frames;
      if (rt_runs > 0 && rt_runs < frames)
      {
         el = now_us() - t_free;
         fl = frames - rt_runs;
      }
      const double speed = (fl * 1001.0 / 60000.0) / (el / 1e6);
      printf("load_ms=%lld runs=%d wall_ms=%lld speed=%.2fx run_max_ms=%.2f\n",
            (long long)(t_load / 1000), fl, (long long)(el / 1000),
            speed, run_max / 1000.0);
      if (min_speed > 0.0 && speed < min_speed)
      {
         printf("FAIL: speed %.2fx, at least %.2fx expected\n", speed,
               min_speed);
         fail = 1;
      }
      printf("audio_total=%ld expected=%ld per_run_min=%ld per_run_max=%ld "
            "cadence_errors=%ld multi_batch_runs=%d\n",
            audio_total, expected_total, audio_min, audio_max,
            cadence_errors, multi_batch_runs);
      printf("video_new=%d video_dupe=%d runs_without_video=%d last=%ux%u\n",
            video_new, video_dupe, runs_without_video, last_w, last_h);
      if (strict && (cadence_errors || multi_batch_runs ||
               runs_without_video || audio_total != expected_total))
      {
         printf("FAIL: not one frame's worth of audio and a video call "
               "every run\n");
         fail = 1;
      }
      printf("audio_hash=%016llx video_hash=%016llx\n",
            (unsigned long long)audio_hash, (unsigned long long)video_hash);
   }
   if (p_mem_data && p_mem_size && p_mem_size(RETRO_MEMORY_SYSTEM_RAM) > 0x300004)
   {
      const unsigned char *ram =
         (const unsigned char *)p_mem_data(RETRO_MEMORY_SYSTEM_RAM);
      uint32_t ctr;
      memcpy(&ctr, ram + 0x300000, 4);
      printf("ram_size=%zu counter_0x300000=%u\n",
            p_mem_size(RETRO_MEMORY_SYSTEM_RAM), ctr);
   }
#ifndef _WIN32
   if (getenv("HARNESS_THREADS"))
   {
      char cmd[64];
      snprintf(cmd, sizeof(cmd), "cat /proc/%d/task/*/comm | sort | uniq -c",
            (int)getpid());
      fflush(stdout);
      if (system(cmd) < 0)
         perror("system");
   }
#endif
   if (wav)
   {
      wav_header(wav, wav_frames);
      fclose(wav);
   }
   if (csv)
      fclose(csv);
   fflush(stdout);
   if (!getenv("HARNESS_NO_UNLOAD"))
   {
      p_unload();
      p_deinit();
   }
   _exit(fail);
}
