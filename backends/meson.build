system_ss.add([files(
  'hostmem.c',
), numa])

if host_os != 'windows'
  if host_os != 'emscripten'
    system_ss.add([files(), rt])
  endif
endif
if have_vhost_user
  system_ss.add(when: 'CONFIG_VIRTIO', if_true: files('vhost-user.c'))
endif
system_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('cryptodev-vhost.c'))
system_ss.add(when: 'CONFIG_IOMMUFD', if_true: files('iommufd.c'))
if have_vhost_user_crypto
  system_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('cryptodev-vhost-user.c'))
endif
system_ss.add(when: gio, if_true: files('dbus-vmstate.c'))
system_ss.add(when: 'CONFIG_SGX', if_true: files('hostmem-epc.c'))
if igvm.found()
  system_ss.add(igvm)
  system_ss.add(files('igvm-cfg.c'), igvm)
  system_ss.add(files('igvm.c'), igvm)
endif

system_ss.add(when: 'CONFIG_SPDM_SOCKET', if_true: files())

subdir('tpm')
