demo_common = static_library('xcoder_demo_common',
  'common/ni_generic_utils.c',
  'common/ni_decode_utils.c',
  'common/ni_encode_utils.c',
  'common/ni_filter_utils.c',
  dependencies : xcoder_dep,
)

demos = ['ni_xcoder_decode', 'ni_xcoder_encode', 'ni_xcoder_scale',
         'ni_xcoder_transcode_filter', 'ni_xcoder_multithread_transcode']
foreach name : demos
  executable(name, '@0@.c'.format(name),
    link_with : demo_common,
    include_directories : 'common',
    dependencies : [xcoder_dep, deps],
  )
endforeach
