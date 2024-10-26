project('ElephantShrew', 
        'cpp',
  version : '0.1',
  default_options : ['warning_level=3']
)

sources = [
  'src/ElephantShrewMain.cpp',
]

exe = executable(
  'elephantshrew',
  sources,
  include_directories : ['inc'],
  install : true)

test('basic', exe)
