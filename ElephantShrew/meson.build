project('ElephantShrew', 
        'cpp',
  version : '0.1',
  default_options : ['buildtype=debug','warning_level=3']
)

spdlog_dep = dependency('spdlog', required: true)

sources = [
  'src/ElephantShrewMain.cpp',
  'src/Bootstrapper.cpp',
  'src/ElephantShrew.cpp',
]

exe = executable(
  'elephantshrew',
  sources,
  include_directories : ['inc','/opt/include'],
  dependencies: [spdlog_dep],
  install : true)

test('basic', exe)
