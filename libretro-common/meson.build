# libretro-common, built as its own library with its own flags: it is
# C89/C99 code that does not follow QEMU's warning set
libretro_common_inc = include_directories('include', '../ui')
libretro_common_src = files('rthreads/rthreads.c',
        'rthreads/retro_eventcount.c',
        'queues/retro_spsc.c',
        'compat/compat_strl.c',
        'encodings/encoding_crc32.c',
        'encodings/encoding_deflate.c',
        'encodings/encoding_xxh3.c',
        'features/features_cpu.c')
if host_os == 'windows'
  libretro_common_src += files('time/rtime.c')
endif
libretro_common_lib = static_library('libretro_common',
  libretro_common_src,
  include_directories: libretro_common_inc,
  c_args: ['-w'],
  pic: true)
libretro_common = declare_dependency(link_with: libretro_common_lib,
                                     include_directories: libretro_common_inc)
