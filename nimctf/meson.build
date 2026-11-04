mctf_dep = dependency('', required : false)

if not get_option('mctf')
  subdir_done()
endif

if not is_linux
  error('MCTF is supported on Linux only')
endif

mctf_library = cc.find_library('nimctf', dirs : meson.current_source_dir())
mctf_dep = declare_dependency(
  dependencies : mctf_library,
  include_directories : '.',
)

install_headers('ni_mctf.h')
install_data('libnimctf.a', install_dir : get_option('libdir'))
pkgconfig.generate(
  name : 'nimctf',
  description : 'NetInt MCTF filter wrapper library',
  version : '1.0',
  libraries : ['-L${libdir}', '-lnimctf'],
  libraries_private : ['-pthread', '-lm'],
)
