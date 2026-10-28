# The xemu libretro core, in plain GNU make: no meson, ninja or python.
#
#   make -j$(nproc)          the core for the system make runs on:
#                            xemu_libretro.dll in an MSYS2 MINGW64 shell,
#                            xemu_libretro.so on Linux
#   make platform=win CROSS_PREFIX=x86_64-w64-mingw32-
#                            the Windows core, cross-compiled
#   make DEBUG=1             unoptimized, with debug information
#   make HAVE_DSP_JIT=0      without the APU DSP's JIT
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
TARGET_NAME  := xemu
DEBUG        ?= 0
HAVE_DSP_JIT ?= 1
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
   DSP_JIT_ARCH      := x86_64
else ifneq ($(findstring aarch64,$(HOST_TRIPLE)),)
   TCG_ARCH          := aarch64
   HOST_INCLUDE_ARCH := aarch64
   ARCH_CFLAGS       :=
   DSP_JIT_ARCH      := aarch64
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
   DSP_JIT_TRIPLE := $(DSP_JIT_ARCH)-unknown-linux-gnu
   DSP_JIT_OS_LIBS :=

   PKGS := glib-2.0 slirp epoxy zlib samplerate
   DEPS_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS)) -pthread
   DEPS_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS)) \
                  -lglslang -lMachineIndependent -lGenericCodeGen -lSPIRV \
                  -lglslang-default-resource-limits -lSPIRV-Tools-opt \
                  -lSPIRV-Tools -lstdc++
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
   OS_LIBS     := -lws2_32 -liphlpapi -lwinmm -lole32 -loleaut32 -lshlwapi \
                  -lshell32 -luuid -ladvapi32 -luser32 -lgdi32 -lcomdlg32 \
                  -lwinspool -lopengl32 -lpathcch -lsynchronization -latomic
   DSP_JIT_TRIPLE := $(DSP_JIT_ARCH)-pc-windows-gnu
   DSP_JIT_OS_LIBS := -lbcrypt -lntdll -luserenv -lws2_32
   VENDORED_DEPS := 1
else
   $(error platform=$(platform) is not built by this Makefile yet: Linux and Windows)
endif

# --- The APU DSP's JIT ---------------------------------------------------
# It runs on mborgerson's dsp56300, a Rust library that comes prebuilt; it
# is fetched into deps/dsp56300 on the first build, as meson does
# (subprojects/dsp56300/meson.build). HAVE_DSP_JIT=0 builds without it.
DSP_JIT_VERSION := 0.1.3
DSP_JIT_NAME    := dsp56300-$(DSP_JIT_VERSION)-$(DSP_JIT_TRIPLE)
DSP_JIT_DIR     := $(CORE_DIR)/deps/dsp56300/$(DSP_JIT_NAME)
DSP_JIT_LIB     := $(DSP_JIT_DIR)/lib/libdsp56300_emu_ffi.a
DSP_JIT_URL     := https://github.com/mborgerson/dsp56300/releases/download/v$(DSP_JIT_VERSION)/$(DSP_JIT_NAME).tar.gz
ifeq ($(HAVE_DSP_JIT),1)
   DSP_JIT_CFLAGS := -DCONFIG_DSP_JIT -I$(DSP_JIT_DIR)/include
   DSP_JIT_LIBS   := $(DSP_JIT_LIB) $(DSP_JIT_OS_LIBS)
   DSP_JIT_DEP    := $(DSP_JIT_LIB)
else
   DSP_JIT_CFLAGS :=
   DSP_JIT_LIBS   :=
   DSP_JIT_DEP    :=
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
OBJS_DSP      := $(call obj,$(SOURCES_DSP) $(SOURCES_DSP_JIT))
OBJS_GLOFF    := $(call obj,$(SOURCES_GLOFF))
OBJS_LRCOMMON := $(call obj,$(SOURCES_LRCOMMON))
OBJS_XXHASH   := $(call obj,$(SOURCES_XXHASH))
OBJS_VOLK     := $(call obj,$(SOURCES_VOLK))
OBJS_SPIRV_REFLECT := $(call obj,$(SOURCES_SPIRV_REFLECT))
OBJS_VSH      := $(call obj,$(SOURCES_VSH))
OBJS_VMA      := $(patsubst %.cc,%.o,$(SOURCES_VMA_CXX))
OBJS_DATA     := $(patsubst %.S,%.o,$(SOURCES_DATA_ASM))

# QEMU's utility code is an archive, as meson builds it: its stubs/ stand in
# only for what nothing else defines
LIBQEMUUTIL := $(CORE_DIR)/libqemuutil.a

OBJECTS := $(OBJS_COMMON) $(OBJS_SYSTEM) $(OBJS_TARGET) \
           $(OBJS_DSP) $(OBJS_GLOFF) $(OBJS_LRCOMMON) $(OBJS_XXHASH) \
           $(OBJS_VOLK) $(OBJS_SPIRV_REFLECT) $(OBJS_VSH) $(OBJS_VMA) \
           $(OBJS_DATA)

$(OBJS_UTIL):     GROUP_CFLAGS := $(CFLAGS_UTIL)
$(OBJS_COMMON):   GROUP_CFLAGS := $(CFLAGS_COMMON)
$(OBJS_SYSTEM):   GROUP_CFLAGS := $(CFLAGS_SYSTEM)
$(OBJS_TARGET):   GROUP_CFLAGS := $(CFLAGS_TARGET)
$(OBJS_DSP):      GROUP_CFLAGS := $(CFLAGS_DSP)
$(OBJS_GLOFF):    GROUP_CFLAGS := $(CFLAGS_GLOFF)
$(OBJS_LRCOMMON): GROUP_CFLAGS := $(CFLAGS_LRCOMMON)
$(OBJS_XXHASH):   GROUP_CFLAGS := $(CFLAGS_XXHASH)
$(OBJS_VOLK):     GROUP_CFLAGS := $(CFLAGS_VOLK)
$(OBJS_SPIRV_REFLECT): GROUP_CFLAGS := $(CFLAGS_SPIRV_REFLECT)
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

$(TARGET): $(OBJECTS) $(LIBQEMUUTIL) $(DSP_JIT_DEP) $(filter %.a,$(DEPS_LIBS))
	$(CXX) -o $@ $(SHARED) $(fpic) $(LDFLAGS) $(OBJECTS) $(LIBQEMUUTIL) \
		$(DSP_JIT_LIBS) $(DEPS_LIBS) $(OS_LIBS)

$(LIBQEMUUTIL): $(OBJS_UTIL)
	rm -f $@
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(BASE_FLAGS) $(GROUP_CFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.cc
	$(CXX) $(BASE_FLAGS) $(GROUP_CXXFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

%.o: %.cpp
	$(CXX) $(BASE_FLAGS) $(GROUP_CXXFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

# .incbin finds data/ from the core's directory
%.o: %.S
	$(CC) $(fpic) -Wa,-I$(CORE_DIR) -c -o $@ $<

$(DSP_JIT_LIB):
	mkdir -p $(CORE_DIR)/deps/dsp56300
	curl -fsSL -o $(CORE_DIR)/deps/dsp56300/$(DSP_JIT_NAME).tar.gz $(DSP_JIT_URL) || \
	wget -q -O $(CORE_DIR)/deps/dsp56300/$(DSP_JIT_NAME).tar.gz $(DSP_JIT_URL) || \
	python3 -c "import sys, urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])" \
		$(DSP_JIT_URL) $(CORE_DIR)/deps/dsp56300/$(DSP_JIT_NAME).tar.gz
	tar -xzf $(CORE_DIR)/deps/dsp56300/$(DSP_JIT_NAME).tar.gz -C $(CORE_DIR)/deps/dsp56300
	rm -f $(CORE_DIR)/deps/dsp56300/$(DSP_JIT_NAME).tar.gz
	touch $@

# The DSP's sources need the JIT's header before they compile
$(OBJS_DSP): | $(DSP_JIT_DEP)

ALL_OBJECTS := $(OBJECTS) $(OBJS_UTIL) $(DEP_OBJECTS)

clean:
	rm -f $(TARGET) $(LIBQEMUUTIL) $(VERSION_H) $(filter %.a,$(DEPS_LIBS))
	rm -f $(ALL_OBJECTS) $(ALL_OBJECTS:.o=.d)

.PHONY: all clean FORCE

-include $(ALL_OBJECTS:.o=.d)
