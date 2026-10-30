fsdev_ss = ss.source_set()
fsdev_ss.add(files())
fsdev_ss.add(when: ['CONFIG_FSDEV_9P'], if_true: files(
  '9p-iov-marshal.c',
  '9p-marshal.c',
  'qemu-fsdev.c',
), if_false: files())
if host_os in ['linux', 'darwin', 'freebsd']
  system_ss.add_all(fsdev_ss)
endif
