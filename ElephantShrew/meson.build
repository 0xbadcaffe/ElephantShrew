project('ElephantShrew', 
        'cpp',
  version : '0.1',
  default_options : ['warning_level=3']
)

spdlog_dep = dependency('spdlog', required: true)
pcapplusplus_dep = dependency('PcapPlusPlus', required: true)

inc_dirs = include_directories('int', 'inc', '/opt/include')

sources = [
  'src/ElephantShrewMain.cpp',
  'src/Bootstrapper.cpp',
  'src/ElephantShrew.cpp',
  'src/NetworkInterfaceScanner.cpp',
  'src/ElephantShrewProcessor.cpp',
  'src/InboundHandler.cpp',
  'src/NetworkInterfaceScanner.cpp',
  'src/OutboundHandler.cpp',
  'src/ElephantShrewSuper.cpp',
  'src/ThreadPool.cpp',
  'src/UdpReceiver.cpp',
  'src/UdpTransmitter.cpp'
]

exe = executable(
  'elephantshrew',
  sources,
  include_directories : inc_dirs,
  dependencies: [spdlog_dep,pcapplusplus_dep],
  install : true)

test('basic', exe)
