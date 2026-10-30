# The xemu libretro core, in plain GNU make: no meson, ninja or python.
#
#   make -j$(nproc)          the core for the system make runs on:
#                            parallel_xbox_libretro.dll in an MSYS2 MINGW64 shell,
#                            parallel_xbox_libretro.so on Linux
#   make platform=win CROSS_PREFIX=x86_64-w64-mingw32-
#                            the Windows core, cross-compiled
#   make ARCH=x86_64         on macOS: the core for that CPU (arm64 or
#                            x86_64; the Mac's own by default)
#   make platform=android NDK=<ndk> ANDROID_ABI=arm64-v8a
#                            the Android core (arm64-v8a or x86_64)
#   make DEBUG=1             unoptimized, with debug information
#   make clean
#
# Makefile.common lists the core's sources and their flags; what QEMU's own
# build would generate is in libretro/gen (libretro/gen/README.md).
#
# GLib is the core's own (libretro/glib), and libslirp is built from deps/
# on it everywhere (Makefile.deps). Windows: every other library the core
# links is built from source in deps/ too, and the DLL needs only Windows'
# own DLLs: nothing but the compiler has to be installed (pacman -S
# mingw-w64-x86_64-gcc make). Linux (x86-64, AArch64): libepoxy comes from
# the system, through pkg-config (README.md, Building). macOS (arm64,
# x86_64): from MacPorts, through pkg-config, as upstream xemu's build
# takes it (scripts/download-macos-libs.py, PKG_CONFIG_LIBDIR pointed at it).

.DEFAULT_GOAL := all

CORE_DIR     := .
TARGET_NAME  := parallel_xbox
DEBUG        ?= 0
PKG_CONFIG   ?= pkg-config

ifeq ($(platform),)
   platform := unix
   UNAME_S := $(shell uname -s)
   ifneq ($(findstring MINGW,$(UNAME_S)),)
      platform := win
   else ifneq ($(findstring MSYS,$(UNAME_S)),)
      platform := win
   else ifneq ($(findstring Darwin,$(UNAME_S)),)
      platform := osx
   endif
endif

# The names libretro's build templates and the old core's Makefile use
ifneq ($(filter win64 windows mingw,$(platform)),)
   override platform := win
endif

# The compilers: the NDK's for Android (NDK, the NDK's directory; the ABI's
# target at API level 29, Android 10), CROSS_PREFIX's, or the system's
# (make's default CC, cc, is not there in MSYS2's MINGW64 shell, gcc is)
ifeq ($(platform),android)
   ANDROID_ABI ?= arm64-v8a
   ifeq ($(ANDROID_ABI),arm64-v8a)
      ANDROID_TARGET := aarch64-linux-android29
   else ifeq ($(ANDROID_ABI),x86_64)
      ANDROID_TARGET := x86_64-linux-android29
   else
      $(error ANDROID_ABI=$(ANDROID_ABI): arm64-v8a or x86_64)
   endif
   ifeq ($(NDK),)
      $(error platform=android needs NDK, the Android NDK's directory)
   endif
   NDK_BIN := $(NDK)/toolchains/llvm/prebuilt/$(shell uname -s | tr A-Z a-z)-x86_64/bin
   CC      := $(NDK_BIN)/$(ANDROID_TARGET)-clang
   CXX     := $(NDK_BIN)/$(ANDROID_TARGET)-clang++
   AR      := $(NDK_BIN)/llvm-ar
else ifneq ($(CROSS_PREFIX),)
   CC  := $(CROSS_PREFIX)gcc
   CXX := $(CROSS_PREFIX)g++
   AR  := $(CROSS_PREFIX)ar
else ifeq ($(platform),win)
   ifeq ($(origin CC),default)
      CC := gcc
   endif
   ifeq ($(origin CXX),default)
      CXX := g++
   endif
endif

# The host's CPU: TCG generates code for it. On macOS the compiler builds
# for whichever -arch it is given, so it is ARCH, not the compiler's own.
ifeq ($(platform),osx)
   ARCH ?= $(shell uname -m)
   HOST_TRIPLE := $(ARCH)-apple-darwin
else
   HOST_TRIPLE := $(shell $(CC) -dumpmachine)
endif
ifneq ($(findstring x86_64,$(HOST_TRIPLE)),)
   TCG_ARCH          := i386
   HOST_INCLUDE_ARCH := x86_64
   ARCH_CFLAGS       := -mcx16 -msse2
else ifneq ($(findstring aarch64,$(HOST_TRIPLE))$(findstring arm64,$(HOST_TRIPLE)),)
   TCG_ARCH          := aarch64
   HOST_INCLUDE_ARCH := aarch64
   ARCH_CFLAGS       :=
else
   $(error the core runs on x86-64 and AArch64 hosts, not $(HOST_TRIPLE))
endif

ifeq ($(platform),unix)
   HOST_OS     := posix
   TARGET      := $(TARGET_NAME)_libretro.so
   fpic        := -fPIC
   SHARED      := -shared -Wl,--version-script=$(CORE_DIR)/ui/libretro.map \
                  -Wl,--no-undefined -Wl,-Bsymbolic -Wl,--exclude-libs,ALL \
                  -Wl,-z,relro -Wl,-z,now -Wl,--gc-sections
   OS_INCFLAGS := -isystem $(CORE_DIR)/linux-headers
   OS_CFLAGS   :=
   OS_LIBS     := -lpthread -ldl -lm -lutil

   PKGS := epoxy
   DEPS_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS)) -pthread
   DEPS_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS)) \
                  -lstdc++
   ifeq ($(DEPS_CFLAGS),-pthread)
      $(error pkg-config found none of: $(PKGS))
   endif
else ifeq ($(platform),win)
   HOST_OS     := win32
   TARGET      := $(TARGET_NAME)_libretro.dll
   fpic        :=
   # -static: libgcc, libstdc++ and winpthread go into the DLL too
   # --discard-all: with a section per function the DLL's symbol table
   # would carry a local symbol for every one of them (.text$name,
   # .pdata$name, .xdata$name: 30 MB instead of 22)
   SHARED      := -shared -static -Wl,--no-undefined \
                  -Wl,--exclude-all-symbols -Wl,--gc-sections -Wl,--discard-all
   OS_INCFLAGS :=
   OS_CFLAGS   := -DWIN32 -DVK_USE_PLATFORM_WIN32_KHR
   # a section per function overflows COFF's 32767 sections in the larger
   # files (libepoxy's generated dispatch): the big object format has room
   OS_SECTION_FLAGS := -Wa,-mbig-obj
   # -lsynchronization: WaitOnAddress/WakeByAddress, which QEMU's own
   # QemuEvent and QemuLockCnt (util/event.c, util/lockcnt.c) are built on
   OS_LIBS     := -lws2_32 -liphlpapi -lwinmm -lole32 -loleaut32 -lshlwapi \
                  -lshell32 -luuid -ladvapi32 -luser32 -lgdi32 -lcomdlg32 \
                  -lwinspool -lopengl32 -lpathcch -lsynchronization
   VENDORED_DEPS := 1
else ifeq ($(platform),osx)
   HOST_OS     := darwin
   TARGET      := $(TARGET_NAME)_libretro.dylib
   fpic        := -fPIC
   # the minimum macOS versions upstream xemu targets
   ifeq ($(ARCH),arm64)
      MACOSX_MIN ?= 13.7.4
   else
      MACOSX_MIN ?= 12.7.5
   endif
   OSX_ARCH_FLAGS := -arch $(ARCH) -mmacosx-version-min=$(MACOSX_MIN)
   # only retro_* are the core's interface, as ui/libretro.map on Linux
   SHARED      := -dynamiclib $(OSX_ARCH_FLAGS) \
                  -Wl,-exported_symbol,_retro_* \
                  -Wl,-headerpad_max_install_names -Wl,-dead_strip
   OS_INCFLAGS :=
   # the Vulkan entry points come from the frontend (MoltenVK), volk loads
   # the rest; Metal surfaces are the frontend's
   OS_CFLAGS   := $(OSX_ARCH_FLAGS) -DVK_USE_PLATFORM_METAL_EXT
   OS_ASFLAGS  := $(OSX_ARCH_FLAGS)
   # CoreFoundation and IOKit: block/file-posix.c's host CD-ROM support
   OS_LIBS     := -lm -lresolv -framework CoreFoundation -framework IOKit \
                  -framework OpenGL

   PKGS := epoxy
   DEPS_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS)) -pthread
   DEPS_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS))
   ifeq ($(DEPS_CFLAGS),-pthread)
      $(error pkg-config found none of: $(PKGS))
   endif
else ifeq ($(platform),android)
   # Vulkan only, through the frontend's context: no GL renderer, and
   # libepoxy (built from deps/, EGL) only for the core's own GL path
   HOST_OS     := posix
   TARGET      := $(TARGET_NAME)_libretro_android.so
   fpic        := -fPIC
   SHARED      := -shared -Wl,--version-script=$(CORE_DIR)/ui/libretro.map \
                  -Wl,--no-undefined -Wl,-Bsymbolic -Wl,--exclude-libs,ALL \
                  -Wl,-z,relro -Wl,-z,now -Wl,--gc-sections \
                  -static-libstdc++
   OS_INCFLAGS := -isystem $(CORE_DIR)/linux-headers
   OS_CFLAGS   := -DVK_USE_PLATFORM_ANDROID_KHR
   OS_LIBS     := -ldl -lm -llog -landroid
   VENDORED_DEPS := 1
else
   $(error platform=$(platform) is not built by this Makefile: unix (Linux), win, osx or android)
endif

# --- Flags ---------------------------------------------------------------

ifeq ($(DEBUG),1)
   OPT := -O0 -g
else
   OPT := -O2
endif

include Makefile.deps
ifeq ($(VENDORED_DEPS),1)
   DEPS_CFLAGS := $(DEPS_VENDORED_CFLAGS)
   DEPS_LIBS   := $(DEP_LIBS)
else
   DEPS_CFLAGS := $(DEPS_VENDORED_CFLAGS) $(DEPS_CFLAGS)
   DEPS_LIBS   := $(DEP_LIBS) $(DEPS_LIBS)
endif

include Makefile.common

# Each function and variable in a section of its own, and the link
# (--gc-sections, -dead_strip on macOS) keeps only what the core's interface
# (retro_*) and QEMU's constructors reach: the QEMU functions nothing in the
# core calls are not in the binary.
SECTION_FLAGS := -ffunction-sections -fdata-sections $(OS_SECTION_FLAGS)
# Every compiled source group, including bundled libraries, must use the
# requested macOS architecture and deployment target.
BASE_FLAGS := $(OPT) $(fpic) $(ARCH_CFLAGS) $(OSX_ARCH_FLAGS) $(SECTION_FLAGS)

# --- Objects -------------------------------------------------------------

obj = $(patsubst %.c,%.o,$(1))

OBJS_UTIL     := $(call obj,$(SOURCES_UTIL) $(SOURCES_HOST))
OBJS_COMMON   := $(call obj,$(SOURCES_COMMON))
OBJS_SYSTEM   := $(call obj,$(SOURCES_SYSTEM))
OBJS_TARGET   := $(call obj,$(SOURCES_TARGET))
OBJS_DSP      := $(call obj,$(SOURCES_DSP))
OBJS_GLOFF    := $(call obj,$(SOURCES_GLOFF))
OBJS_LRCOMMON := $(call obj,$(SOURCES_LRCOMMON))
OBJS_GLIB     := $(call obj,$(SOURCES_GLIB))
OBJS_VOLK     := $(call obj,$(SOURCES_VOLK))
OBJS_SPIRV_REFLECT := $(call obj,$(SOURCES_SPIRV_REFLECT))
OBJS_GLSLC    := $(call obj,$(SOURCES_GLSLC))
OBJS_VSH      := $(call obj,$(SOURCES_VSH))
OBJS_VMA      := $(patsubst %.cc,%.o,$(SOURCES_VMA_CXX))
OBJS_DATA     := $(patsubst %.S,%.o,$(SOURCES_DATA_ASM))

# QEMU's utility code is an archive, as meson builds it: its stubs/ stand in
# only for what nothing else defines
LIBQEMUUTIL := $(CORE_DIR)/libqemuutil.a

OBJECTS := $(OBJS_COMMON) $(OBJS_SYSTEM) $(OBJS_TARGET) \
           $(OBJS_DSP) $(OBJS_GLOFF) $(OBJS_LRCOMMON) $(OBJS_GLIB) \
           $(OBJS_VOLK) $(OBJS_SPIRV_REFLECT) $(OBJS_GLSLC) $(OBJS_VSH) \
           $(OBJS_VMA) \
           $(OBJS_DATA)

$(OBJS_UTIL):     GROUP_CFLAGS := $(CFLAGS_UTIL)
$(OBJS_COMMON):   GROUP_CFLAGS := $(CFLAGS_COMMON)
$(OBJS_SYSTEM):   GROUP_CFLAGS := $(CFLAGS_SYSTEM)
$(OBJS_TARGET):   GROUP_CFLAGS := $(CFLAGS_TARGET)
$(OBJS_DSP):      GROUP_CFLAGS := $(CFLAGS_DSP)
$(OBJS_GLOFF):    GROUP_CFLAGS := $(CFLAGS_GLOFF)
$(OBJS_LRCOMMON): GROUP_CFLAGS := $(CFLAGS_LRCOMMON)
$(OBJS_GLIB):     GROUP_CFLAGS := $(CFLAGS_GLIB)
$(OBJS_VOLK):     GROUP_CFLAGS := $(CFLAGS_VOLK)
$(OBJS_SPIRV_REFLECT): GROUP_CFLAGS := $(CFLAGS_SPIRV_REFLECT)
$(OBJS_GLSLC):    GROUP_CFLAGS := $(CFLAGS_GLSLC)
$(OBJS_VSH):      GROUP_CFLAGS := $(CFLAGS_VSH)
$(OBJS_VMA):      GROUP_CXXFLAGS := $(CXXFLAGS_VMA)
# MinGW's __try1/__except1 (util/oslib-win32.c) put the exception handler's
# directives in .text, which the assembler rejects in a function's own
# section: that file keeps one .text
$(CORE_DIR)/util/oslib-win32.o: BASE_FLAGS := $(OPT) $(fpic) $(ARCH_CFLAGS)

# --- Rules ---------------------------------------------------------------

all: $(TARGET)

# The version and commit the core logs at start: made at every build,
# rewritten only when they change (libretro/version.sh)
VERSION_H := $(GEN_DIR)/xemu-version-macro.h
$(VERSION_H): FORCE
	@sh $(CORE_DIR)/libretro/version.sh $(CORE_DIR) $@
$(CORE_DIR)/xemu-version.o: $(VERSION_H)
FORCE:

$(TARGET): $(OBJECTS) $(LIBQEMUUTIL) $(filter %.a,$(DEPS_LIBS))
	$(CXX) -o $@ $(SHARED) $(fpic) $(LDFLAGS) $(OBJECTS) $(LIBQEMUUTIL) \
		$(DEPS_LIBS) $(OS_LIBS)

$(LIBQEMUUTIL): $(OBJS_UTIL)
	rm -f $@
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(BASE_FLAGS) $(GROUP_CFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.cc
	$(CXX) $(BASE_FLAGS) $(GROUP_CXXFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

# .incbin finds data/ from the core's directory
%.o: %.S
	$(CC) $(fpic) $(OS_ASFLAGS) -Wa,-I$(CORE_DIR) -c -o $@ $<

ALL_OBJECTS := $(OBJECTS) $(OBJS_UTIL) $(DEP_OBJECTS)

clean:
	rm -f $(TARGET) $(LIBQEMUUTIL) $(VERSION_H) $(filter %.a,$(DEPS_LIBS))
	rm -f $(ALL_OBJECTS) $(ALL_OBJECTS:.o=.d) $(DSPDIFF) $(SEEDSHADERS) $(S3TCTEST) $(S3TCTEST).exe

# The DSP JIT against the interpreter (libretro/tests/dspdiff.c), built
# from the DSP's own sources, nothing else of the core: a plain program,
# for the host or for another (make dspdiff CC=aarch64-linux-gnu-gcc,
# run under qemu-aarch64)
DSPDIFF := $(CORE_DIR)/libretro/tests/dspdiff
DSPDIFF_SRC := $(CORE_DIR)/libretro/tests/dspdiff.c \
	$(CORE_DIR)/hw/xbox/mcpx/apu/dsp/dsp_c.c \
	$(CORE_DIR)/hw/xbox/mcpx/apu/dsp/dsp_dma.c \
	$(CORE_DIR)/hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c \
	$(CORE_DIR)/hw/xbox/mcpx/apu/dsp/jit/dspjit.c
$(DSPDIFF): $(DSPDIFF_SRC)
	$(CC) -O2 $(QEMU_CFLAGS) $(ARCH_CFLAGS) -I$(CORE_DIR)/hw/xbox/mcpx/apu/dsp \
		-I$(CORE_DIR)/hw/xbox/mcpx/apu/dsp/interp -o $@ $(DSPDIFF_SRC)
dspdiff: $(DSPDIFF)

# Every seed-dictionary shader through the GLSL to SPIR-V compiler
# (libretro/tests/seedshaders.c), linked with the core's own generators and
# compiler objects: built after the core, for the host
SEEDSHADERS := $(CORE_DIR)/libretro/tests/seedshaders
SEEDSHADERS_OBJS := $(call obj,$(addprefix $(CORE_DIR)/hw/xbox/nv2a/pgraph/, \
		glsl/common.c glsl/geom.c glsl/psh.c glsl/shaders.c glsl/vsh.c \
		glsl/vsh-ff.c glsl/vsh-prog.c glsl/uniform-cache.c texture.c)) \
	$(OBJS_GLSLC) $(OBJS_VSH) $(OBJS_GLIB) $(OBJS_LRCOMMON)
$(SEEDSHADERS): $(CORE_DIR)/libretro/tests/seedshaders.c $(SEEDSHADERS_OBJS) \
		$(LIBQEMUUTIL)
	$(CC) -O2 $(CFLAGS_TARGET) -o $@ $< $(filter-out $<,$^) \
		$(DEPS_LIBS) $(OS_LIBS)
seedshaders: $(SEEDSHADERS)

S3TCTEST := $(CORE_DIR)/libretro/tests/s3tctest
$(S3TCTEST): $(CORE_DIR)/libretro/tests/test-s3tc.c \
		$(CORE_DIR)/hw/xbox/nv2a/pgraph/s3tc.o $(OBJS_GLIB) $(OBJS_LRCOMMON) \
		$(LIBQEMUUTIL)
	$(CC) -O2 $(CFLAGS_TARGET) -o $@ $< \
		$(CORE_DIR)/hw/xbox/nv2a/pgraph/s3tc.o $(OBJS_GLIB) $(OBJS_LRCOMMON) \
		$(DEPS_LIBS) $(OS_LIBS) -lm
s3tctest: $(S3TCTEST)

.PHONY: all clean dspdiff seedshaders s3tctest FORCE

-include $(ALL_OBJECTS:.o=.d)
