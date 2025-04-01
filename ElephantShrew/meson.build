project('ElephantShrew', 
        'cpp',
  version : '0.1',
  default_options : ['cpp_std=c++20','warning_level=3']
)

#boost_dep = dependency('boost', required: true)
openssl = dependency('openssl', required: true)
spdlog_dep = dependency('spdlog', required: true)
pcapplusplus_dep = dependency('PcapPlusPlus', required: true)

inc_dirs = include_directories('int', 'inc', '/opt/include')

sources = [
  'src/ElephantShrewMain.cpp',
  'src/Bootstrapper.cpp',
  'src/ElephantShrew.cpp',
  #'src/NetworkInterfaceScanner.cpp',
  'src/DataProcessor.cpp',
  'src/InboundHandler.cpp',
  'src/OutboundHandler.cpp',
  'src/ElephantShrewSuper.cpp',
  #'src/ThreadPool.cpp',
  'src/UdpReceiver.cpp',
  'src/UdpTransmitter.cpp',
  'src/boost_redis_tester.cpp'
]

exe = executable(
  'elephantshrew',
  sources,
  include_directories : inc_dirs,
  dependencies: [openssl,spdlog_dep,pcapplusplus_dep],
  cpp_args : ['-DBOOST_ASIO_HAS_CO_AWAIT'],
  install : true)

test('basic', exe)
