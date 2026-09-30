mctf_dep = dependency('', required : false)

if not get_option('mctf')
  subdir_done()
endif

if not is_linux_like
  error('MCTF is only supported on Linux')
endif

mctf = cc.find_library('nimctf', dirs : meson.current_source_dir())
mctf_dep = declare_dependency(
  dependencies : mctf,
  include_directories : '.',
)

install_headers('ni_mctf.h')
install_data('libnimctf.a', install_dir : get_option('libdir'))
pkgconfig.generate(
  name : 'nimctf',
  description : 'NetInt MCTF filter wrapper library',
  version : '1.0',
  libraries : '-lnimctf',
  libraries_private : ['-pthread', '-lm'],
)
