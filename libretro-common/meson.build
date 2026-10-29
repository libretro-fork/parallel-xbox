# libretro-common, built as its own library with its own flags: it is
# C89/C99 code that does not follow QEMU's warning set
libretro_common_inc = include_directories('include')
libretro_common_src = files('rthreads/rthreads.c',
        'rthreads/retro_eventcount.c',
        'queues/retro_spsc.c',
        'compat/compat_posix_string.c',
        'compat/compat_strcasestr.c',
        'compat/compat_strl.c',
        'compat/fopen_utf8.c',
        'encodings/encoding_crc32.c',
        'encodings/encoding_deflate.c',
        'encodings/encoding_utf.c',
        'encodings/encoding_xxh3.c',
        'features/features_cpu.c',
        'file/file_path.c',
        'file/file_path_io.c',
        'streams/file_stream.c',
        'string/rstrtod.c',
        'string/stdstring.c',
        'time/rtime.c',
        'vfs/vfs_implementation.c')
# HAVE_MMAP: the VFS maps files through mmap; on Windows through its own
# file mappings, which need no define. path_is_absolute is a name QEMU's
# block layer has too (block.c): libretro-common's is renamed here, in
# every file of it, so the two can share the core.
libretro_common_args = ['-w', '-Dpath_is_absolute=lrc_path_is_absolute']
if host_os != 'windows'
  libretro_common_args += ['-DHAVE_MMAP']
endif
libretro_common_lib = static_library('libretro_common',
  libretro_common_src,
  include_directories: libretro_common_inc,
  c_args: libretro_common_args,
  pic: true)
libretro_common = declare_dependency(link_with: libretro_common_lib,
                                     include_directories: libretro_common_inc)
