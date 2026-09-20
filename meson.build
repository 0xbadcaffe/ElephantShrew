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
  'src/DataProcessor.cpp',
  'src/InboundHandler.cpp',
  'src/OutboundHandler.cpp',
  'src/ElephantShrewSuper.cpp',
  'src/UdpReceiver.cpp',
  'src/UdpTransmitter.cpp',
  'src/boost_redis_tester.cpp',
  'src/RedisPacketStore.cpp',
  'src/PcapReceiver.cpp',
]

# Insert after the existing sources array and before executable(...).
# Keep the original Boost/OpenSSL/PcapPlusPlus/spdlog configuration unchanged.
ebpf_deps = []
ebpf_generated = []
build_cfg = configuration_data()
build_cfg.set10('ES_HAS_EBPF', get_option('ebpf'))
build_cfg.set10('ES_HAS_TCX', get_option('ebpf') and get_option('ebpf_tcx'))
build_cfg.set10('ES_HAS_ACTIVE_TC', get_option('ebpf') and get_option('ebpf_active'))
build_cfg.set10('ES_HAS_XDP', get_option('ebpf') and get_option('ebpf_xdp'))
build_cfg.set10('ES_HAS_AF_XDP', get_option('ebpf') and get_option('af_xdp'))

if not get_option('ebpf') and (get_option('ebpf_tcx') or get_option('ebpf_active') or
                             get_option('ebpf_xdp') or get_option('af_xdp'))
  error('TCX/active/XDP/AF_XDP options require -Debpf=true')
endif

if get_option('ebpf')
  if host_machine.system() != 'linux'
    error('eBPF backend requires Linux')
  endif
  fs = import('fs')
  bpf_clang = find_program(get_option('bpf_clang'), native: true)
  bpftool = find_program(get_option('bpf_tool'), native: true)
  bpf_dep = dependency('libbpf', version: '>=1.0', method: 'pkg-config')
  ebpf_deps += [bpf_dep, dependency('libelf'), dependency('zlib'), dependency('threads')]
  bpf_target = host_machine.endian() == 'little' ? 'bpfel' : 'bpfeb'
  probe = run_command(bpf_clang, '-target', bpf_target, '-O2', '-x', 'c',
                      '-c', '/dev/null', '-o', '/dev/null', check: false)
  if probe.returncode() != 0
    error('Selected Clang cannot compile BPF: ' + probe.stderr())
  endif
  if get_option('ebpf_tcx') and not cpp.has_function('bpf_program__attach_tcx',
      prefix: '#include <bpf/libbpf.h>', dependencies: bpf_dep)
    error('TCX requested but installed libbpf lacks bpf_program__attach_tcx')
  endif

  btf_file = get_option('bpf_btf')
  if btf_file == ''
    if meson.is_cross_build()
      error('Cross-builds require an explicit -Dbpf_btf=... baseline')
    endif
    btf_file = '/sys/kernel/btf/vmlinux'
  endif
  if not fs.is_file(btf_file)
    error('BTF baseline not found: ' + btf_file)
  endif
  vmlinux_h = custom_target('es-vmlinux',
    input: btf_file, output: 'vmlinux.h',
    command: [bpftool, 'btf', 'dump', 'file', '@INPUT@', 'format', 'c'],
    capture: true)

  variants = [['es_tc_legacy', 'bpf/elephantshrew_tc.bpf.c', []]]
  bpf_headers = files('bpf/elephantshrew_shared.h', 'bpf/packet_parser.bpf.h')
  if get_option('ebpf_tcx')
    variants += [['es_tc_tcx', 'bpf/elephantshrew_tc.bpf.c', ['-DES_TCX=1']]]
  endif
  if get_option('ebpf_active')
    bpf_headers += files('bpf/elephantshrew_policy_shared.h')
    variants += [['es_active_legacy', 'bpf/elephantshrew_active_tc.bpf.c', []]]
    if get_option('ebpf_tcx')
      variants += [['es_active_tcx', 'bpf/elephantshrew_active_tc.bpf.c', ['-DES_TCX=1']]]
    endif
  endif
  if get_option('ebpf_xdp')
    variants += [['es_xdp', 'bpf/elephantshrew_xdp.bpf.c', []]]
  endif
  if get_option('af_xdp')
    ebpf_deps += [dependency('libxdp', method: 'pkg-config')]
    variants += [['es_xsk', 'bpf/elephantshrew_xsk.bpf.c', []]]
    sources += files('src/AfXdpReceiver.cpp')
  endif
  bpf_include = bpf_dep.get_variable(pkgconfig: 'includedir')
  foreach item : variants
    name = item[0]
    obj = custom_target(name + '-object',
      input: item[1], output: name + '.bpf.o',
      command: [bpf_clang, '-target', bpf_target, '-D__BPF__=1',
                '-O2', '-g', '-Wall', '-Werror',
                '-I' + meson.current_build_dir(),
                '-I' + join_paths(meson.current_source_dir(), 'bpf'),
                '-I' + bpf_include] + item[2] + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
      depends: vmlinux_h, depend_files: bpf_headers)
    skel = custom_target(name + '-skeleton',
      input: obj, output: name + '.skel.h',
      command: [bpftool, 'gen', 'skeleton', '@INPUT@', 'name', name],
      capture: true)
    ebpf_generated += [skel]
  endforeach
  inc_dirs += include_directories('bpf')
  sources += files('src/EbpfObject.cpp', 'src/EbpfCapabilities.cpp',
                   'src/EbpfTelemetry.cpp', 'src/EbpfManager.cpp')
  if get_option('ebpf_active')
    sources += files('src/EbpfPolicyManager.cpp')
  endif
  if get_option('ebpf_xdp') or get_option('af_xdp')
    sources += files('src/XdpAttachment.cpp')
  endif
endif
configure_file(output: 'es_build_config.h', configuration: build_cfg)

exe = executable(
    'elephantshrew',
    sources + ebpf_generated,
    include_directories: inc_dirs,
    dependencies: [openssl, spdlog_dep, pcapplusplus_dep, boost_dep] + ebpf_deps,
    install: true)

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

test('help', exe, args: ['--help'], timeout: 5)
if get_option('ebpf_tests')
  subdir('tests/ebpf')
endif
