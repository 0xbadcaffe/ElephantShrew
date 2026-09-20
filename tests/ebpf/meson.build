host_test_includes = include_directories('../../bpf', '../../inc', '../../int')
foreach name : ['abi_cpp', 'parser_test', 'parser_fuzz', 'decoder_test', 'queue_test']
  t = executable('ebpf-' + name, name + '.cpp',
    include_directories: host_test_includes,
    dependencies: dependency('threads'),
    cpp_args: ['-UNDEBUG'],
    install: false)
  test('ebpf-' + name, t, timeout: 60, suite: 'ebpf-host')
endforeach
