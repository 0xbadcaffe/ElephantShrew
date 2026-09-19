# 05 — Meson and the BPF build pipeline

ElephantShrew eBPF implementation series · kernel artifacts and userspace build boundary

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on the source files from 01–03; 04 is optional. Keep the existing application build working with `ebpf=false`. Clang compiles BPF C; the selected host C++ compiler builds ElephantShrew. Host `-march=native`, LTO, and application flags must not leak into the BPF command.

The following are **integration fragments**, not a replacement for the entire existing build file. The baseline already defines `cpp`, `sources`, `inc_dirs`, and the existing dependency objects [repo-build].

## File changes

```text
meson_options.txt                      [EDIT: append options]
meson.build                            [EDIT: generation targets and dependencies]
.gitignore                             [EDIT: ignore build products, not bpf sources]
build/<profile>/vmlinux.h               [GENERATED]
build/<profile>/es_tc_legacy.bpf.o       [GENERATED]
build/<profile>/es_tc_legacy.skel.h      [GENERATED]
build/<profile>/es_build_config.h       [GENERATED]
```

## Development prerequisites

On a Debian development host, install the BPF toolchain in addition to the existing project dependencies:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends     clang llvm libbpf-dev libelf-dev zlib1g-dev bpftool     pkg-config meson ninja-build iproute2 ethtool
# For the optional AF_XDP milestone, also install the distribution's libxdp-dev.
clang --print-targets | grep -i bpf
pkg-config --modversion libbpf
bpftool version
```

Confirm package availability for the selected distro release rather than replacing system packages blindly. A binary named `clang` may lack the BPF target. A build container need not have the host's `/sys/kernel/btf/vmlinux`; supply a versioned baseline BTF file explicitly instead.

### `meson_options.txt` — append

```meson
# Append to existing meson_options.txt; retain native_optimizations.
option('ebpf', type: 'boolean', value: false)
option('ebpf_tcx', type: 'boolean', value: false)
option('ebpf_active', type: 'boolean', value: false)
option('ebpf_xdp', type: 'boolean', value: false)
option('af_xdp', type: 'boolean', value: false)
option('bpf_btf', type: 'string', value: '')
option('bpf_clang', type: 'string', value: 'clang')
option('bpf_tool', type: 'string', value: 'bpftool')
option('ebpf_tests', type: 'boolean', value: false)
```

### `meson.build` — generation fragment

```meson
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
```

### Extend the existing executable definition

```meson
exe = executable('elephantshrew', sources + ebpf_generated,
  include_directories: inc_dirs,
  dependencies: [openssl, spdlog_dep, pcapplusplus_dep, boost_dep] + ebpf_deps,
  install: true)
```

`capture: true` saves bpftool stdout directly into the generated header. The skeleton embeds the object and receives an explicit name, so `es_tc_legacy.skel.h` exports `es_tc_legacy__open()` / `__load()` / `__destroy()` [custom], [skeleton]. Do not add `>` shell redirection as an argument to Meson's command array.

Include `es_build_config.h` at optional integration sites. Use `#if ES_HAS_EBPF`, not `#ifdef ES_HAS_EBPF`, because a disabled feature is defined as zero.

## Build matrix

After adding the source files from chapters 06–09:

```bash
meson setup build/pcap -Debpf=false
meson compile -C build/pcap

meson setup build/ebpf -Debpf=true -Debpf_tcx=false
meson compile -C build/ebpf

meson setup build/ebpf-tcx -Debpf=true -Debpf_tcx=true
meson compile -C build/ebpf-tcx

# Reproducible build: supply a versioned file and selected tools.
meson setup build/ebpf-repro -Debpf=true   -Dbpf_btf=/opt/elephantshrew/btf/baseline.btf   -Dbpf_clang=/usr/bin/clang -Dbpf_tool=/usr/sbin/bpftool
```

The last paths are deployment choices; substitute installed paths. Record the BTF file hash, compiler version, libbpf version, bpftool version, target endianness, build options, and source commit in release metadata. Do not call CO-RE a substitute for a missing helper, attachment API, or driver action.

For cross-compilation, Meson's **host machine** is the machine that runs ElephantShrew; Clang and bpftool are build-machine tools. The BPF target uses host endianness. The target libbpf headers and linked library must be compatible. An explicit BTF baseline is part of the target contract, not whichever kernel happens to run the build container.

## Optional features are gated, not stubs pretending to work

Keep `ebpf_active`, `ebpf_xdp`, and `af_xdp` false until their manager/receiver source files exist and are tested. Enabling one before implementing those files should fail the build rather than produce a binary advertising a nonexistent feature. The public feature matrix in chapter 08 must reflect compiled **and integrated** support, not just the presence of a `.bpf.o` file.

## Tests and acceptance gate

Remove `test('basic', exe)`: it launches a long-running process. Replace it with the terminating `--help` and `--check-config` tests introduced in 08 and wired in 10. Keep all privileged attachment tests out of the default unprivileged suite.

Touching a BPF header must rebuild its object and skeleton; touching a C++ implementation must not regenerate unrelated BPF artifacts. The disabled build must not probe Clang, bpftool, BTF, or libbpf. A missing BPF target or invalid BTF baseline must fail with a specific message before compilation starts.

## References

[repo-build]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/meson.build
[custom]: https://mesonbuild.com/Reference-manual_functions_custom_target.html
[skeleton]: https://docs.kernel.org/bpf/libbpf/libbpf_overview.html
[debian]: https://packages.debian.org/trixie/bpftool

Baseline: [current Meson build][repo-build]. Tool contracts: [Meson generation targets][custom], [libbpf skeletons][skeleton], and [Debian bpftool package][debian].
---

[04 — Policy, mirroring, and XDP datapaths](04_policy_mirroring_and_xdp.md) · [06 — libbpf loader and attachment lifecycle](06_libbpf_loader_and_attachment_lifecycle.md)
