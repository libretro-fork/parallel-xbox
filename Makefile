# The xemu libretro core, in plain GNU make: no meson, ninja or python.
#
#   make -j$(nproc)          the core for the system make runs on:
#                            parallel_xbox_libretro.dll in an MSYS2 MINGW64 shell,
#                            parallel_xbox_libretro.so on Linux
#   make platform=win CROSS_PREFIX=x86_64-w64-mingw32-
#                            the Windows core, cross-compiled
#   make DEBUG=1             unoptimized, with debug information
#   make clean
#
# Makefile.common lists the core's sources and their flags; what QEMU's own
# build would generate is in libretro/gen (libretro/gen/README.md).
#
# Windows: every library the core links is built from source in deps/
# (Makefile.deps), and the DLL needs only Windows' own DLLs: nothing but
# the compiler has to be installed (pacman -S mingw-w64-x86_64-gcc make).
# Linux (x86-64, AArch64): the libraries come from the system, through
# pkg-config (README.md, Building).

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

# The compilers: CROSS_PREFIX's, or the system's (make's default CC, cc,
# is not there in MSYS2's MINGW64 shell, gcc is)
ifneq ($(CROSS_PREFIX),)
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

# The host's CPU: TCG generates code for it
HOST_TRIPLE := $(shell $(CC) -dumpmachine)
ifneq ($(findstring x86_64,$(HOST_TRIPLE)),)
   TCG_ARCH          := i386
   HOST_INCLUDE_ARCH := x86_64
   ARCH_CFLAGS       := -mcx16 -msse2
else ifneq ($(findstring aarch64,$(HOST_TRIPLE)),)
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
                  -Wl,-z,relro -Wl,-z,now
   OS_INCFLAGS := -isystem $(CORE_DIR)/linux-headers
   OS_CFLAGS   :=
   OS_LIBS     := -lpthread -ldl -lm -lutil

   PKGS := glib-2.0 slirp epoxy
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
   SHARED      := -shared -static -Wl,--no-undefined \
                  -Wl,--exclude-all-symbols
   OS_INCFLAGS :=
   OS_CFLAGS   := -DWIN32 -DVK_USE_PLATFORM_WIN32_KHR
   # -lsynchronization: WaitOnAddress/WakeByAddress, which QEMU's own
   # QemuEvent and QemuLockCnt (util/event.c, util/lockcnt.c) are built on
   OS_LIBS     := -lws2_32 -liphlpapi -lwinmm -lole32 -loleaut32 -lshlwapi \
                  -lshell32 -luuid -ladvapi32 -luser32 -lgdi32 -lcomdlg32 \
                  -lwinspool -lopengl32 -lpathcch -lsynchronization
   VENDORED_DEPS := 1
else
   $(error platform=$(platform) is not built by this Makefile yet: Linux and Windows)
endif

# --- Flags ---------------------------------------------------------------

ifeq ($(DEBUG),1)
   OPT := -O0 -g
else
   OPT := -O2
endif

ifeq ($(VENDORED_DEPS),1)
   include Makefile.deps
   DEPS_CFLAGS := $(DEPS_VENDORED_CFLAGS)
   DEPS_LIBS   := $(DEP_LIBS)
endif

include Makefile.common

BASE_FLAGS := $(OPT) $(fpic) $(ARCH_CFLAGS)

# --- Objects -------------------------------------------------------------

obj = $(patsubst %.c,%.o,$(1))

OBJS_UTIL     := $(call obj,$(SOURCES_UTIL) $(SOURCES_HOST))
OBJS_COMMON   := $(call obj,$(SOURCES_COMMON))
OBJS_SYSTEM   := $(call obj,$(SOURCES_SYSTEM))
OBJS_TARGET   := $(call obj,$(SOURCES_TARGET))
OBJS_DSP      := $(call obj,$(SOURCES_DSP))
OBJS_GLOFF    := $(call obj,$(SOURCES_GLOFF))
OBJS_LRCOMMON := $(call obj,$(SOURCES_LRCOMMON))
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
           $(OBJS_DSP) $(OBJS_GLOFF) $(OBJS_LRCOMMON) \
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
$(OBJS_VOLK):     GROUP_CFLAGS := $(CFLAGS_VOLK)
$(OBJS_SPIRV_REFLECT): GROUP_CFLAGS := $(CFLAGS_SPIRV_REFLECT)
$(OBJS_GLSLC):    GROUP_CFLAGS := $(CFLAGS_GLSLC)
$(OBJS_VSH):      GROUP_CFLAGS := $(CFLAGS_VSH)
$(OBJS_VMA):      GROUP_CXXFLAGS := $(CXXFLAGS_VMA)

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
	$(CC) $(fpic) -Wa,-I$(CORE_DIR) -c -o $@ $<

ALL_OBJECTS := $(OBJECTS) $(OBJS_UTIL) $(DEP_OBJECTS)

clean:
	rm -f $(TARGET) $(LIBQEMUUTIL) $(VERSION_H) $(filter %.a,$(DEPS_LIBS))
	rm -f $(ALL_OBJECTS) $(ALL_OBJECTS:.o=.d) $(DSPDIFF) $(SEEDSHADERS)

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
	$(OBJS_GLSLC) $(OBJS_VSH) \
	$(call obj,$(CORE_DIR)/libretro-common/encodings/encoding_deflate.c \
		$(CORE_DIR)/libretro-common/encodings/encoding_crc32.c \
		$(CORE_DIR)/libretro-common/encodings/encoding_xxh3.c \
		$(CORE_DIR)/libretro-common/features/features_cpu.c)
$(SEEDSHADERS): $(CORE_DIR)/libretro/tests/seedshaders.c $(SEEDSHADERS_OBJS) \
		$(LIBQEMUUTIL)
	$(CC) -O2 $(CFLAGS_TARGET) -o $@ $< $(filter-out $<,$^) \
		$(DEPS_LIBS) $(OS_LIBS)
seedshaders: $(SEEDSHADERS)

.PHONY: all clean dspdiff seedshaders FORCE

-include $(ALL_OBJECTS:.o=.d)
