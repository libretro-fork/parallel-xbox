block_ss.add(files())
if host_os == 'linux'
  block_ss.add(files('pr-manager.c'))
else
  block_ss.add(files('pr-manager-stub.c'))
endif
