project('xcoder', 'c', 'cpp',
  version : '5.9.0',
  meson_version : '>= 0.62',
  default_options : [
    'prefix=/usr/local',
    'libdir=lib',
    'bindir=bin',
    'includedir=include',
    'c_std=gnu99',
    'cpp_std=c++11',
    'buildtype=debugoptimized',
  ],
)

pkgconfig = import('pkgconfig')

cc = meson.get_compiler('c')
system = host_machine.system()
is_windows = system == 'windows'
is_darwin = system == 'darwin'
is_linux = system == 'linux'

if not is_windows and not is_darwin and not is_linux
  error('libxcoder Meson builds currently support Linux, Windows, and macOS only')
endif

# The product revision is the ABI/pkg-config version.  It is deliberately
# independent from meson.project_version(), which describes the source release.
xcoder_revision_define = cc.get_define(
  'NI_XCODER_REVISION',
  prefix : '#include <ni_defs.h>',
  args : ['-I' + meson.current_source_dir() / 'source'],
)
xcoder_revision_parts = xcoder_revision_define.split('"')
if (xcoder_revision_parts.length() != 3 or
    xcoder_revision_parts[0] != '' or
    xcoder_revision_parts[2] != '')
  error('source/ni_defs.h does not contain a valid NI_XCODER_REVISION string')
endif
xcoder_revision = xcoder_revision_parts[1]
if xcoder_revision.substring(2, 3) == ''
  error('NI_XCODER_REVISION must contain at least three characters')
endif
message('libxcoder product revision: ' + xcoder_revision)

common_args = ['-DLIBXCODER_OBJS_BUILD']
if is_darwin
  # Match Makefile's unconditional Darwin WERROR_FLAGS.  Unlike other
  # platforms, Darwin does not define NI_WARN_AS_ERROR for -e.
  common_args += ['-Werror', '-Wno-unused-command-line-argument']
elif get_option('werror')
  common_args += '-DNI_WARN_AS_ERROR'
endif
if get_option('secure-compile')
  common_args += ['-Wall', '-fstack-protector-strong']
  add_project_link_arguments(
    cc.get_supported_link_arguments(
      '-Wl,-z,now',
      '-Wl,-z,relro',
      '-Wl,-z,noexecstack',
    ),
    language : ['c', 'cpp'],
  )
endif

# This option changes declarations visible to consumers as well as the library.
public_args = []
if get_option('deprecation-as-error')
  public_args += '-DDEPRECATION_AS_ERROR'
endif

add_project_arguments(common_args + public_args, language : ['c', 'cpp'])

platform_deps = [dependency('threads')]
math_dep = cc.find_library('m', required : false)
if math_dep.found()
  platform_deps += math_dep
endif

if is_windows
  platform_deps += cc.find_library('ws2_32')
  add_project_link_arguments('-Wl,--stack,4194304', language : ['c', 'cpp'])
elif is_linux
  rt_dep = cc.find_library('rt', required : false)
  if rt_dep.found()
    platform_deps += rt_dep
  endif
  dl_dep = cc.find_library('dl', required : false)
  if dl_dep.found()
    platform_deps += dl_dep
  endif
endif

if get_option('cpu-affinity')
  if not is_linux
    error('cpu-affinity is supported on Linux only')
  endif
  numa_dep = dependency('numa', required : false)
  if not numa_dep.found()
    numa_dep = cc.find_library('numa')
  endif
  platform_deps += numa_dep
endif

if get_option('linux-virt-io-driver') and not is_linux
  error('linux-virt-io-driver is supported on Linux only')
endif
if get_option('systemd') and not is_linux
  error('systemd installation is supported on Linux only')
endif

subdir('nimctf')
subdir('source')

if get_option('systemd')
  current_user = run_command(
    find_program('whoami'),
    check : true,
  ).stdout().strip()
  current_group = run_command(
    find_program('id'),
    '-gn',
    check : true,
  ).stdout().strip()
  service = configure_file(
    input : 'nilibxcoder.service',
    output : 'nilibxcoder.service',
    command : [
      find_program('sed'),
      '-e',
      's|/usr/local/bin|@0@|'.format(get_option('prefix') / get_option('bindir')),
      '-e',
      's|^User=.*|User=@0@|'.format(current_user),
      '-e',
      's|^Group=.*|Group=@0@|'.format(current_group),
      '@INPUT@',
    ],
    capture : true,
  )
  install_data(service, install_dir : get_option('systemdir'))

  # Match build.sh: enabling this option starts the service during install.
  # It therefore requires a live systemd instance and is not suitable for
  # staged or chroot installations.
  systemctl = find_program('systemctl')
  meson.add_install_script(
    systemctl,
    'enable',
    'nilibxcoder.service',
  )
  meson.add_install_script(
    systemctl,
    'start',
    'nilibxcoder.service',
  )
endif
