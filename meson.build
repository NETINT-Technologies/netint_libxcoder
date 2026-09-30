project('xcoder', 'c', 'cpp',
  version : '5.8.2',
  meson_version : '>= 0.62',
  default_options : [ 'c_std=gnu99',
                      'cpp_std=c++11',
                      'buildtype=debugoptimized' ])

pkgconfig = import('pkgconfig')

cc = meson.get_compiler('c')
system = host_machine.system()
is_windows = system == 'windows'
is_darwin = system == 'darwin'
is_linux_like = not is_windows and not is_darwin

common_args = ['-DLIBXCODER_OBJS_BUILD']
if get_option('werror')
  common_args += '-DNI_WARN_AS_ERROR'
endif
if is_darwin
  common_args += '-Wno-unused-command-line-argument'
endif
if get_option('secure-compile')
  common_args += ['-Wall', '-fstack-protector-strong']
  add_project_link_arguments(
    cc.get_supported_link_arguments('-Wl,-z,now', '-Wl,-z,relro', '-Wl,-z,noexecstack'),
    language : ['c', 'cpp'])
endif

# Affects public struct layout, consumers need it too.
public_args = []
if get_option('deprecation-as-error')
  public_args += '-DDEPRECATION_AS_ERROR'
endif

add_project_arguments(common_args + public_args, language : ['c', 'cpp'])

deps = [dependency('threads'), cc.find_library('m', required : false)]
if is_windows
  deps += cc.find_library('ws2_32')
  add_project_link_arguments('-Wl,--stack,4194304', language : ['c', 'cpp'])
elif is_linux_like
  deps += [cc.find_library('rt', required : false), dependency('dl')]
endif
if get_option('cpu-affinity')
  numa_dep = dependency('numa', required : false)
  if not numa_dep.found()
    numa_dep = cc.find_library('numa')
  endif
  deps += numa_dep
endif

subdir('nimctf')
subdir('source')

if get_option('systemd')
  service = configure_file(
    input : 'nilibxcoder.service',
    output : 'nilibxcoder.service',
    command : [find_program('sed'),
               '-e', 's|/usr/local/bin|@0@|'.format(get_option('prefix') / get_option('bindir')),
               '@INPUT@'],
    capture : true,
  )
  install_data(service, install_dir : get_option('systemdir'))
endif
