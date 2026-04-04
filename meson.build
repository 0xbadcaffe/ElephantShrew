project('ElephantShrew', 
        'cpp',
  version : '0.1',
  default_options : ['cpp_std=c++20','warning_level=3']
)

buildtype = get_option('buildtype')
cpp = meson.get_compiler('cpp')

project_cpp_args = ['-DBOOST_ASIO_HAS_CO_AWAIT']

if cpp.get_id() in ['gcc', 'clang']
  project_cpp_args += [
    '-Wpedantic',
    '-Wshadow',
    '-Wformat=2',
    '-Wconversion',
    '-Wnull-dereference',
    '-Wsign-conversion',
  ]

  if get_option('native_optimizations') and buildtype == 'release'
    project_cpp_args += [
      '-march=native',
      '-mtune=native',
      '-fomit-frame-pointer',
    ]
  endif
endif

if cpp.get_id() == 'clang'
  project_cpp_args += ['-DFMT_CONSTEVAL=']
endif

add_project_arguments(project_cpp_args, language : 'cpp')

boost_dep = dependency('boost', required: true, include_type : 'system')
openssl = dependency('openssl', required: true, include_type : 'system')
spdlog_dep = dependency('spdlog', required: true, include_type : 'system')
pcapplusplus_dep = dependency('PcapPlusPlus', required: true, include_type : 'system')

inc_dirs = include_directories('int', 'inc')

sources = [
  'src/ElephantShrewMain.cpp',
  'src/Bootstrapper.cpp',
  'src/ElephantShrew.cpp',
  'src/RuntimeConfig.cpp',
  #'src/NetworkInterfaceScanner.cpp',
  'src/DataProcessor.cpp',
  'src/InboundHandler.cpp',
  'src/OutboundHandler.cpp',
  'src/ElephantShrewSuper.cpp',
  #'src/ThreadPool.cpp',
  'src/UdpReceiver.cpp',
  'src/UdpTransmitter.cpp',
  'src/boost_redis_tester.cpp',
  'src/RedisPacketStore.cpp',
  'src/PcapReceiver.cpp',
]

exe = executable(
  'elephantshrew',
  sources,
  include_directories : inc_dirs,
  dependencies: [openssl, spdlog_dep, pcapplusplus_dep, boost_dep],
  install : true)

bash = find_program('bash')

run_target(
  'clang-tidy-report',
  command : [
    bash,
    join_paths(meson.project_source_root(), 'scripts', 'run_clang_tidy.sh'),
    meson.project_source_root(),
    meson.project_build_root(),
    join_paths(meson.project_source_root(), 'reports', 'clang-tidy'),
  ],
)

run_target(
  'cppcheck-report',
  command : [
    bash,
    join_paths(meson.project_source_root(), 'scripts', 'run_cppcheck.sh'),
    meson.project_source_root(),
    meson.project_build_root(),
    join_paths(meson.project_source_root(), 'reports', 'cppcheck'),
  ],
)

run_target(
  'valgrind-report',
  command : [
    bash,
    join_paths(meson.project_source_root(), 'scripts', 'run_valgrind.sh'),
    meson.project_build_root(),
    join_paths(meson.project_source_root(), 'reports', 'valgrind'),
    '-s',
  ],
)

test('basic', exe)
