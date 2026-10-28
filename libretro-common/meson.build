# libretro-common, built as its own library with its own flags: it is
# C89/C99 code that does not follow QEMU's warning set
libretro_common_inc = include_directories('include')
libretro_common_lib = static_library('libretro_common',
  files('rthreads/rthreads.c',
        'rthreads/retro_eventcount.c',
        'queues/retro_spsc.c'),
  include_directories: libretro_common_inc,
  c_args: ['-w'],
  pic: true)
libretro_common = declare_dependency(link_with: libretro_common_lib,
                                     include_directories: libretro_common_inc)
