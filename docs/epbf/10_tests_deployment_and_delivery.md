# 10 — Tests, deployment, and delivery

ElephantShrew eBPF implementation series · verification, deployment, and implementation handoff

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.


## Objective

Turn each chapter into a reviewable change with a test gate. Host tests check the shared ABI, parser, queue, and decoder. A separate privileged suite checks loading, attachment ownership, connectivity, and teardown on actual target kernels. Passing a host parser test is not a BPF verifier result.

## File structure changes

```text
tests/ebpf/
  abi_c.c                         FROM 01
  abi_cpp.cpp                     FROM 01
  parser_test.cpp                 ADD: deterministic protocol/length cases
  parser_fuzz.cpp                 ADD: seeded randomized bounds checks
  decoder_test.cpp                ADD: size/version/semantic validation
  queue_test.cpp                  ADD: capacity, wraparound, two-thread order
  meson.build                     ADD: ordinary host tests, no root required
  observe_lab.sh                  ADD: privileged isolated namespace test
  config_test.cpp                 IMPLEMENT: cases specified in 08
  program_test.cpp                IMPLEMENT: BPF_PROG_TEST_RUN path below
  ownership_test.sh               IMPLEMENT: preexisting policy and crash matrix
packaging/
  elephantshrew.service           ADD after capability/stop validation
meson.build                       MODIFY: remove infinite basic test, include tests
```

## 1. Complete host test files

Do not compile these assertion-based tests with `NDEBUG`; assertions are their checks. The parser intentionally does not validate checksums or fully parse arbitrary extension chains, TCP options, or encrypted payloads. Tests below exercise the contract in 02, not a claim of universal protocol validation.

### `tests/ebpf/parser_test.cpp` — complete file

```cpp
#include "packet_parser.bpf.h"
#include <cassert>
#include <cstdint>
#include <vector>
using Bytes = std::vector<__u8>;
static void U16(Bytes& b, std::size_t at, unsigned value) {
    b.at(at) = static_cast<__u8>(value >> 8);
    b.at(at + 1) = static_cast<__u8>(value);
}
static Bytes V4(bool tcp = false) {
    Bytes b(tcp ? 54 : 42, 0);
    U16(b, 12, 0x0800); b[14] = 0x45;
    U16(b, 16, tcp ? 40 : 28); b[23] = tcp ? 6 : 17;
    b[26] = 192; b[29] = 1; b[30] = 192; b[33] = 2;
    U16(b, 34, 12345); U16(b, 36, 443);
    if (tcp) b[46] = 0x50; else U16(b, 38, 8);
    return b;
}
static Bytes V6(unsigned extensions = 0) {
    Bytes b(62 + extensions * 8, 0);
    U16(b, 12, 0x86dd); b[14] = 0x60;
    U16(b, 18, 8 + extensions * 8); b[20] = extensions ? 0 : 17;
    b[22] = 0x20; b[38] = 0x20;
    for (unsigned i = 0; i < extensions; ++i)
        b[54 + i * 8] = i + 1 == extensions ? 17 : 0;
    const auto off = 54 + extensions * 8;
    U16(b, off, 12345); U16(b, off + 2, 443); U16(b, off + 4, 8);
    return b;
}
static Bytes Tagged(const Bytes& b, unsigned n) {
    Bytes out(b.begin(), b.begin() + 12);
    for (unsigned i = 0; i < n; ++i) {
        out.push_back(0x81); out.push_back(0x00);
        out.push_back(0x00); out.push_back(static_cast<__u8>(i + 1));
    }
    out.insert(out.end(), b.begin() + 12, b.end());
    return out;
}
static int Parse(const Bytes& b, es_tuple& t, int stripped = 0) {
    return es_parse(b.data(), static_cast<__u32>(b.size()),
                    static_cast<__u32>(b.size()), stripped, 7, &t);
}
int main() {
    es_tuple t{};
    auto b = V4();
    assert(Parse(b, t) == ES_PARSE_OK);
    assert(t.flags & ES_T_PORTS_VALID);
    assert(es_be16(reinterpret_cast<const __u8*>(&t.sport)) == 12345);
    assert(t.ip_version == 4 && t.l4_proto == 17);
    U16(b, 20, 0x4000); // DF alone is not a fragment.
    assert(Parse(b, t) == ES_PARSE_OK);
    U16(b, 20, 0x2000);
    assert(Parse(b, t) == ES_PARSE_FRAGMENT && !(t.flags & ES_T_PORTS_VALID));
    U16(b, 20, 1);
    assert(Parse(b, t) == ES_PARSE_FRAGMENT);
    b = V4(); b[14] = 0x44;
    assert(Parse(b, t) == ES_PARSE_MALFORMED);
    b = V4(); U16(b, 16, 1000);
    assert(Parse(b, t) == ES_PARSE_MALFORMED);
    b = V4(); U16(b, 38, 7);
    assert(Parse(b, t) == ES_PARSE_MALFORMED);
    b = V4(true);
    assert(Parse(b, t) == ES_PARSE_OK && t.l4_proto == 6);
    b[46] = 0x40;
    assert(Parse(b, t) == ES_PARSE_MALFORMED);
    b = V4();
    for (__u32 cap = 0; cap < 42; ++cap)
        assert(es_parse(b.data(), cap, 42, 0, 0, &t) == ES_PARSE_TRUNCATED);
    Bytes short_frame(4, 0);
    assert(Parse(short_frame, t) == ES_PARSE_MALFORMED);
    for (unsigned n = 0; n <= 2; ++n) {
        auto tag = Tagged(V4(), n);
        assert(Parse(tag, t) == ES_PARSE_OK && t.vlan_count == n);
    }
    auto tag = Tagged(V4(), 3);
    assert(Parse(tag, t) == ES_PARSE_UNSUPPORTED);
    tag = Tagged(V4(), 1);
    assert(Parse(tag, t, 1) == ES_PARSE_OK && t.vlan_count == 2 && t.vlan[0] == 7);
    tag = Tagged(V4(), 2);
    assert(Parse(tag, t, 1) == ES_PARSE_UNSUPPORTED);
    for (unsigned n = 0; n <= 4; ++n) {
        auto ip6 = V6(n);
        assert(Parse(ip6, t) == ES_PARSE_OK && t.ip_version == 6);
    }
    b = V6(5);
    assert(Parse(b, t) == ES_PARSE_UNSUPPORTED);
    b = V6(1); b[20] = 44;
    assert(Parse(b, t) == ES_PARSE_FRAGMENT && !(t.flags & ES_T_PORTS_VALID));
    b = V6(); b[20] = 50;
    assert(Parse(b, t) == ES_PARSE_UNSUPPORTED);
    b = V6(); U16(b, 18, 0);
    assert(Parse(b, t) == ES_PARSE_UNSUPPORTED);
    b = V6(); b[14] = 0x40;
    assert(Parse(b, t) == ES_PARSE_MALFORMED);
    Bytes arp(42, 0);
    U16(arp, 12, 0x0806); U16(arp, 14, 1); U16(arp, 16, 0x0800);
    arp[18] = 6; arp[19] = 4; arp[28] = 192; arp[38] = 192;
    assert(Parse(arp, t) == ES_PARSE_OK && (t.flags & ES_T_ADDR_VALID));
    assert(!(t.flags & ES_T_PORTS_VALID));
    Bytes unknown(14, 0); U16(unknown, 12, 0x88b5);
    assert(Parse(unknown, t) == ES_PARSE_UNSUPPORTED);
}
```
### `tests/ebpf/parser_fuzz.cpp` — complete file

```cpp
#include "packet_parser.bpf.h"
#include <cassert>
#include <cstdint>
#include <random>
#include <vector>
int main() {
    std::mt19937 rng(0xe1e5u);
    for (unsigned run = 0; run < 100000; ++run) {
        const __u32 cap = rng() % (ES_PARSE_BYTES + 1);
        // The allocation is exactly cap bytes: ASan sees actual overreads.
        std::vector<__u8> b(cap);
        for (auto& v : b) v = static_cast<__u8>(rng());
        if (cap >= 14 && run % 2 == 0) { b[12] = 0x08; b[13] = 0x00; }
        if (cap >= 14 && run % 3 == 0) { b[12] = 0x86; b[13] = 0xdd; }
        const __u32 original = cap + (rng() % 2048);
        es_tuple t{};
        const int rc = es_parse(b.data(), cap, original, rng() % 2, rng() & 0xfff, &t);
        assert(rc >= ES_PARSE_OK && rc <= ES_PARSE_FRAGMENT);
        assert(t.vlan_count <= 2);
        if (t.flags & ES_T_PORTS_VALID) {
            assert((t.flags & ES_T_ADDR_VALID) && !(t.flags & ES_T_FRAGMENT));
            assert(t.l4_proto == 6 || t.l4_proto == 17);
        }
    }
}
```
### `tests/ebpf/decoder_test.cpp` — complete file

```cpp
#include "TelemetryTypes.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace ElephantShrew;
static es_event Good() {
    es_event e{};
    e.abi_version = ES_ABI_VERSION; e.record_size = sizeof(e);
    e.ifindex = 3; e.original_len = 42; e.hook = ES_HOOK_TC;
    e.link_type = ES_LINKTYPE_ETHERNET; e.parse_status = ES_PARSE_OK;
    return e;
}
int main() {
    auto e = Good(); es_event out{};
    assert(DecodeEvent(&e, sizeof(e), out));
    assert(!DecodeEvent(nullptr, sizeof(e), out));
    assert(!DecodeEvent(&e, sizeof(e) - 1, out));
    e.abi_version = 1; assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.record_size = 32; assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.direction = 2; assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.hook = 99; assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.captured_len = 43; assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.flags = ES_E_COPY_FAILED; e.captured_len = 1;
    assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.tuple.flags = ES_T_PORTS_VALID;
    assert(!DecodeEvent(&e, sizeof(e), out));
    e = Good(); e.captured_len = 4; e.prefix[0] = 0x42;
    std::array<unsigned char, sizeof(e) + 1> unaligned{};
    std::memcpy(unaligned.data() + 1, &e, sizeof(e));
    assert(DecodeEvent(unaligned.data() + 1, sizeof(e), out) && out.prefix[0] == 0x42);
}
```
### `tests/ebpf/queue_test.cpp` — complete file

```cpp
#include "SpscQueue.hpp"
#include <cassert>
#include <cstdint>
#include <thread>
using ElephantShrew::SpscQueue;
int main() {
    bool threw = false;
    try { SpscQueue<std::uint64_t> bad(0); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    SpscQueue<std::uint64_t> one(1);
    std::uint64_t v = 0;
    assert(!one.TryPop(v)); assert(one.TryPush(9)); assert(!one.TryPush(10));
    assert(one.TryPop(v) && v == 9); assert(!one.TryPop(v));
    SpscQueue<std::uint64_t> q(31);
    constexpr std::uint64_t count = 250000;
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i)
            while (!q.TryPush(i)) std::this_thread::yield();
    });
    for (std::uint64_t i = 0; i < count; ++i) {
        while (!q.TryPop(v)) std::this_thread::yield();
        assert(v == i);
    }
    producer.join();
    assert(!q.TryPop(v));
}
```

### Build and run without a BPF-capable kernel

After creating the files from the earlier chapters in the repository:

```bash
mkdir -p build/ebpf-host-tests
cc -std=c11 -Wall -Wextra -Werror -Ibpf tests/ebpf/abi_c.c \
    -o build/ebpf-host-tests/abi_c
./build/ebpf-host-tests/abi_c
for name in abi_cpp parser_test parser_fuzz decoder_test queue_test; do
    c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -UNDEBUG \
        -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
        -Ibpf -Iinc -Iint "tests/ebpf/$name.cpp" \
        -o "build/ebpf-host-tests/$name"
    "build/ebpf-host-tests/$name"
done
```

The randomized test performs 100,000 deterministic iterations with precisely sized buffers. It is a useful regression test, not exhaustive fuzzing. Add coverage-guided fuzzing with a saved corpus and packet fixtures as a follow-up implementation task. The queue test checks 250,000 ordered transfers; it does not prove a data-race-free implementation on every architecture. Run a supported ThreadSanitizer configuration separately rather than mixing it with AddressSanitizer.

### `tests/ebpf/meson.build`

The C ABI test above can stay a standalone C compiler check, keeping the application's project language C++ only. Use separate Meson executables for the C++ unit tests:

### `tests/ebpf/meson.build` — complete file

```meson
host_test_includes = include_directories('../../bpf', '../../inc', '../../int')
foreach name : ['abi_cpp', 'parser_test', 'parser_fuzz', 'decoder_test', 'queue_test']
  t = executable('ebpf-' + name, name + '.cpp',
    include_directories: host_test_includes,
    dependencies: dependency('threads'),
    cpp_args: ['-UNDEBUG'],
    install: false)
  test('ebpf-' + name, t, timeout: 60, suite: 'ebpf-host')
endforeach
```

At the end of the root Meson file, add `if get_option('ebpf_tests')` / `subdir('tests/ebpf')` / `endif`. The host tests do not require `ebpf=true`. Replace `test('basic', exe)` with a terminating `--help` smoke test **after implementing `--help`**; the baseline executable does not support that flag yet. [build]

```meson
# Integration fragment in root meson.build, after implementing chapter 08:
test('help', exe, args: ['--help'], timeout: 5)
if get_option('ebpf_tests')
  subdir('tests/ebpf')
endif
```

## 2. Verifier and program tests

Build all enabled object variants with the actual BPF compiler from 05. Load each supported object on every declared kernel/architecture combination and archive verifier logs. Verify that unsupported TCX sections are not loaded when selecting legacy TC. Do not consider successful `bpftool gen skeleton` equivalent to successful kernel load.

Use `bpf_prog_test_run_opts()` for supported program types to exercise known packet bytes and context. The following **integration fragment** assumes the files and loader from 06 and a valid Ethernet test packet. It loads a program but does not attach to a device:

```cpp
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/bpf.h>
#include <cstdint>
#include <stdexcept>
#include "EbpfObject.hpp"

// Supply a valid packet fixture and call inside a dedicated privileged test.
void CheckPassiveVerdict(const void* packet, std::uint32_t packet_size) {
    ElephantShrew::ObjectOptions options;
    options.sample_every = 0;
    options.flow_enabled = false;
    options.egress = false;
    auto obj = ElephantShrew::LoadObserver(options, ElephantShrew::TcApi::Legacy);
    __sk_buff context{};
    context.ifindex = 1; // Synthetic test identity, not a claim about attachment.
    bpf_test_run_opts run{};
    run.sz = sizeof(run);
    run.data_in = packet;
    run.data_size_in = packet_size;
    run.ctx_in = &context;
    run.ctx_size_in = sizeof(context);
    run.repeat = 1;
    if (bpf_prog_test_run_opts(bpf_program__fd(obj.ingress), &run) != 0)
        throw std::runtime_error("BPF test run failed; report errno and kernel log");
    if (run.retval != static_cast<std::uint32_t>(-1))
        throw std::runtime_error("passive TC observer did not continue classification");
}
```

Read the resulting per-CPU counters and assert packet/byte deltas. Expand the test to sampling, parser status, snapshot length, full rings, exact policy matches, and policy generation changes. Supported test contexts and program-type behavior depend on the kernel; consult the [kernel test-run documentation][testrun]. Test-run does not replace actual TC/XDP device attachment or target-driver tests.

## 3. Isolated passive integration lab

This lab uses **three dedicated network namespaces**. The bridge lives inside `es-mid`, not on the host's management network. It deliberately supplies connectivity independently of ElephantShrew so a passive observer must not change it. It refuses to reuse namespaces, tracks created resources, and cleans only those resources.

The script expects the `eBPF ready` and periodic stats log contracts described in 09. Replace log parsing with a structured status endpoint once one is implemented. It is a test script for the proposed executable, not a command that the current repository already passes.

### `tests/ebpf/observe_lab.sh` — complete file

```bash
#!/usr/bin/env bash
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "Run this isolated lab as root in a test VM." >&2; exit 1; }
[[ $# -eq 1 ]] || { echo "Usage: $0 /absolute/path/to/elephantshrew" >&2; exit 1; }
BIN=$(realpath "$1")
[[ -x $BIN ]] || { echo "Not executable: $BIN" >&2; exit 1; }
command -v ip >/dev/null
command -v tc >/dev/null
S=es-src D=es-dst M=es-mid
for ns in "$S" "$D" "$M"; do
    if ip netns list | awk '{print $1}' | grep -qx "$ns"; then
        echo "Refusing to reuse namespace: $ns" >&2; exit 1
    fi
done
TMP=$(mktemp -d)
created=()
pid=
cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    if [[ -n ${pid:-} ]] && kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        for _ in {1..50}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    for ns in "${created[@]}"; do ip netns del "$ns" 2>/dev/null || true; done
    if [[ $rc -ne 0 ]]; then
        echo "Lab failed; logs retained in $TMP" >&2
        [[ ! -f $TMP/service.log ]] || tail -60 "$TMP/service.log" >&2
    else
        rm -rf "$TMP"
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for ns in "$S" "$D" "$M"; do ip netns add "$ns"; created+=("$ns"); done
ip -n "$M" link add es-left type veth peer name src0
ip -n "$M" link set src0 netns "$S"
ip -n "$M" link add es-right type veth peer name dst0
ip -n "$M" link set dst0 netns "$D"
ip -n "$M" link add es-br type bridge
for dev in es-left es-right; do
    ip -n "$M" link set "$dev" master es-br
    ip -n "$M" link set "$dev" up
done
ip -n "$M" link set es-br up
for ns in "$S" "$D" "$M"; do ip -n "$ns" link set lo up; done
ip -n "$S" link set src0 up
ip -n "$D" link set dst0 up
ip -n "$S" addr add 192.0.2.1/24 dev src0
ip -n "$D" addr add 192.0.2.2/24 dev dst0
ip netns exec "$S" ping -q -c 3 -W 1 192.0.2.2
cat >"$TMP/observe.json" <<'JSON'
{
  "capture": {"interfaces": ["es-left", "es-right"], "backend": "none"},
  "routing": {"enabled": false, "engine": "pcap"},
  "ebpf": {
    "enabled": true, "hook": "tc", "attach_api": "legacy", "mode": "observe",
    "required": true, "directions": ["ingress", "egress"], "sample_every": 1,
    "snapshot_bytes": 0, "ring_buffer_bytes": 1048576,
    "event_queue_capacity": 8192, "stats_interval_ms": 1000,
    "drain_timeout_ms": 3000, "classify": true, "flow_enabled": false,
    "max_flows": 4096, "select_l4_proto": 0
  },
  "ui": {"show_startup_art": false}
}
JSON
ip netns exec "$M" "$BIN" -c "$TMP/observe.json" >"$TMP/service.log" 2>&1 &
pid=$!
ready=0
for _ in {1..100}; do
    kill -0 "$pid" 2>/dev/null || { echo "Service exited before ready" >&2; exit 1; }
    if grep -q 'eBPF ready' "$TMP/service.log"; then ready=1; break; fi
    sleep 0.1
done
[[ $ready == 1 ]] || { echo "No ready indication" >&2; exit 1; }
ip netns exec "$S" ping -q -c 200 -i 0.01 -W 1 192.0.2.2
stats=0
for _ in {1..50}; do
    if grep -Eq 'eBPF stats iface=es-left direction=ingress packets=[1-9][0-9]*' "$TMP/service.log"; then
        stats=1; break
    fi
    sleep 0.1
done
[[ $stats == 1 ]] || { echo "No positive ingress counter" >&2; exit 1; }
kill -TERM "$pid"
for _ in {1..100}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$pid" 2>/dev/null; then echo "Shutdown did not complete" >&2; exit 1; fi
wait "$pid"
pid=
for dev in es-left es-right; do
    for direction in ingress egress; do
        filters=$(ip netns exec "$M" tc filter show dev "$dev" "$direction")
        if grep -Eq 'es_ingress|es_egress' <<<"$filters"; then
            echo "Observer filter remains: $dev/$direction" >&2; exit 1
        fi
    done
done
ip netns exec "$S" ping -q -c 3 -W 1 192.0.2.2
echo "PASS: passive multi-interface connectivity, counters, and normal cleanup"
```

Run in a dedicated test VM after implementing the application integration:

```bash
chmod +x tests/ebpf/observe_lab.sh
sudo tests/ebpf/observe_lab.sh "$(pwd)/build/ebpf/elephantshrew"
```

For **forwarding** tests, remove the bridge and its port membership, then let exactly one ElephantShrew engine connect `es-left` and `es-right`. Baseline connectivity must fail without that engine and succeed with it. Leaving the test bridge in place can make a broken forwarder appear to work.

For **coexistence** tests, install an independently owned TC filter with a known match/action, attach ElephantShrew at its documented position, and verify that the other filter's behavior and identity survive normal shutdown and partial startup failure. Test a competing writer replacing a legacy handle before cleanup: ElephantShrew must report the ownership conflict rather than removing that writer's program.

## 4. Acceptance matrix

| Layer | Must pass before enabling the feature |
|---|---|
| Optional build | `ebpf=false` needs no BPF tools/BTF/libbpf; `ebpf=true` builds all declared enabled variants. |
| Configuration | All negative/precedence cases from 08; no attach on validation failure; existing pcap CLI still works. |
| ABI/parser | C/C++ layout; truncated and malformed inputs; VLAN metadata; fragments; IPv6 work bound; decoder rejects unsupported versions. |
| Passive TC | All traffic continues under baseline conditions; another owner's policy still applies; selected interface/direction labels are correct. |
| Backpressure | Full ring and full userspace queue increase separate telemetry losses; slow storage does not stall the BPF hook. |
| Shutdown | Ctrl+C, SIGTERM, partial attachment failure, blocked recorder admission, repeated starts, and bounded cooperative sink cancellation. |
| Crash/ownership | Kill the process; check TCX versus legacy lifetime; recover only recorded owned filters; leave shared qdiscs intact. |
| Interfaces | Hot-unplug/recreation, namespace identity, name/index reuse; no stale redirection target. |
| Flows | Bounded memory, per-CPU summation, LRU eviction explicitly best-effort, no duplicate snapshot accumulation. |
| Active TC | Default/rule behavior, generation publication, malformed/fragment traffic, mirror failure, redirect target removal, no duplicate pcap reinjection. |
| XDP | Requested native/generic mode, action support on both devices, coexistence, target MTU, and actual redirect failures. |
| AF_XDP | Queue bindings, copy/zero-copy reporting, missing fill buffers, recycling, leases, shutdown with in-flight descriptors, and explicit packet disposition. |

Do not require every advanced feature to ship with the first passive release. Do require unavailable features to fail clearly rather than accepting configuration that does nothing.

### Benchmarks

Compare identical workloads for pcap-only, TC counters-only, TC sampled metadata, hybrid recording, and any enabled active backend. Record kernel, compiler, libbpf, firmware/driver, CPU count, interface mode, MTU, and offloads. Measure network delivery, capture loss, telemetry loss, storage loss, CPU, memory, and latency separately.

TC counters observe skbs, not an unconditional one-to-one count of Ethernet frames on the wire. Segmentation and receive offloads affect comparisons; use independent endpoint measurements and report the offload configuration. Never describe a full ring as network packet loss unless a separately measured network path actually lost packets. [offloads]

## 5. Deployment example

This service unit is a **starting point for a capability-tested passive deployment**, not a universal permission recipe. Create the service user and configuration directory explicitly; install the built executable at the indicated path. Modern networking-BPF operations commonly use `CAP_BPF` and `CAP_NET_ADMIN`; kernel/security policy and the selected backend can change requirements. Pcap packet sockets additionally need the appropriate raw-socket permission. Test on the target rather than granting every capability. [caps]

### `packaging/elephantshrew.service` — complete file

```ini
[Unit]
Description=ElephantShrew network telemetry
After=network.target

[Service]
Type=simple
User=elephantshrew
Group=elephantshrew
ExecStart=/usr/local/bin/elephantshrew -c /etc/elephantshrew/observe.json
Restart=on-failure
RestartSec=2
TimeoutStopSec=15
RuntimeDirectory=elephantshrew
StateDirectory=elephantshrew
AmbientCapabilities=CAP_BPF CAP_NET_ADMIN
CapabilityBoundingSet=CAP_BPF CAP_NET_ADMIN
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
# Do not enable PrivateNetwork: this service must observe the selected namespace.
# Add CAP_NET_RAW only for a backend that actually needs packet/raw sockets.

[Install]
WantedBy=multi-user.target
```

Keep credentials out of startup summaries and telemetry events. Packet prefixes can contain sensitive data; default to metadata-only, enforce retention/access controls, and use bounded queues. A production privilege-separated loader is preferable when practical, but it needs an explicit authenticated control protocol and ownership model, not merely running the same process under another user.

`Type=simple` reports that a process started, not that every observer is attached. Use the ready/health state from 09 for operational monitoring; add a proper systemd notification integration before switching to `Type=notify`. Choose one documented supervisor policy: either application retries with visible degraded health, or process exit plus systemd restart. Do not advertise an unhealthy internally retrying process as fully ready.

The application's common shutdown deadline must fit inside the service timeout, with room for detach diagnostics. A stuck uninterruptible operation cannot be made safe by a timeout value in a unit file. A crash on legacy TC still requires ownership-aware recovery on the next startup.

## 6. Resulting repository layout

These are proposed additions/edits. Unrelated existing source files remain in place.

```text
ElephantShrew/
├── bpf/
│   ├── elephantshrew_shared.h
│   ├── packet_parser.bpf.h
│   ├── elephantshrew_tc.bpf.c
│   ├── elephantshrew_policy_shared.h       [optional active]
│   ├── elephantshrew_active_tc.bpf.c       [optional active]
│   ├── elephantshrew_xdp.bpf.c             [optional XDP]
│   └── elephantshrew_xsk.bpf.c             [optional AF_XDP]
├── inc/
│   ├── EbpfOptions.hpp
│   ├── EbpfObject.hpp
│   ├── EbpfCapabilities.hpp
│   ├── TcAttachment.hpp
│   ├── SpscQueue.hpp
│   ├── TelemetryTypes.hpp
│   ├── EbpfTelemetry.hpp
│   ├── EbpfManager.hpp
│   ├── AfXdpReceiver.hpp                   [later receiver]
│   ├── RuntimeConfig.hpp                  [modified]
│   ├── ElephantShrew.hpp                  [modified]
│   └── Bootstrapper.hpp                   [modified]
├── int/
│   ├── ITelemetrySink.hpp
│   ├── IPacketStore.hpp                    [modified]
│   └── IReceiver.hpp                       [modified for later receiver]
├── src/
│   ├── EbpfObject.cpp
│   ├── EbpfCapabilities.cpp
│   ├── EbpfTelemetry.cpp
│   ├── EbpfManager.cpp                     [assemble 06–09]
│   ├── EbpfPolicyManager.cpp                   [implement active control plane]
│   ├── XdpAttachment.cpp                   [implement XDP ownership]
│   ├── AfXdpReceiver.cpp                   [implement AF_XDP ownership]
│   ├── RuntimeConfig.cpp                  [modified]
│   ├── ElephantShrew.cpp                   [modified]
│   ├── ElephantShrewMain.cpp               [modified]
│   ├── Bootstrapper.cpp                    [modified]
│   ├── PcapReceiver.cpp                    [modified]
│   └── RedisPacketStore.cpp                [modified lifecycle, name retained]
├── configs/
│   ├── ebpf-observe.json
│   └── ebpf-hybrid.json
├── tests/ebpf/                             [unit + integration tests above]
├── packaging/elephantshrew.service
├── docs/ebpf-implementation/               [these ten Markdown files]
├── meson_options.txt                       [modified]
└── meson.build                             [modified]
```

Generated `vmlinux.h`, `*.bpf.o`, `*.skel.h`, and `es_build_config.h` belong in the **build directory**, not in the source tree. Keep the selected BTF baseline as a separately versioned build input when reproducibility/cross-builds require it.

## 7. Commit-sized delivery plan

| Change | Main chapters | Reviewable result |
|---|---|---|
| ABI + host tests | 01–02, 10 | Stable v2 structures, bounded parser, host tests pass. |
| Optional BPF build | 03, 05 | Disabled build unchanged; passive object/skeleton generation works. |
| Loader and telemetry | 06–07 | Owned attachments, bounded queue, validated events, cumulative counters. |
| Configuration | 08 | Strict merged validation, terminating help/check modes, explicit backends. |
| Service integration | 09 | Required-observer startup, worker health, no recording shutdown deadlock. |
| Passive release gate | 10 | Multi-interface lab, coexistence, saturation, crash/restart checks. |
| Active TC | 04, 08–10 | Explicit policy, atomic per-object generations, mirror/redirect tests. |
| XDP / AF_XDP | 04–10 | Enable each backend only after target-device and buffer-ownership validation. |

The file numbers describe the architecture from low level to high level, not an instruction to enable all active features before the passive application works. Keep experimental options disabled while integrating the first release.

## 8. Validation performed while preparing this series

| Check performed on the supplied snippets | Result |
|---|---|
| C shared ABI compilation and executable | Passed with GCC; layout assertions compiled. |
| C++ shared ABI compilation and executable | Passed with GCC; AddressSanitizer and UndefinedBehaviorSanitizer enabled. |
| Pure-C parser header syntax | Passed with GCC C11 and warnings treated as errors. |
| Deterministic parser tests | Passed with AddressSanitizer/UndefinedBehaviorSanitizer. |
| Seeded randomized parser bounds tests | 100,000 iterations passed with AddressSanitizer/UndefinedBehaviorSanitizer. |
| Event decoder validation and unaligned input | Passed with AddressSanitizer/UndefinedBehaviorSanitizer. |
| SPSC capacity/wraparound and two-thread FIFO test | 250,000 ordered transfers passed with AddressSanitizer/UndefinedBehaviorSanitizer. |
| Markdown code-fence structure, internal file links, JSON examples, and Bash syntax | Checked during packaging. |

**Not executed here:** a full ElephantShrew build, Meson configuration, BPF-target compilation, verifier/program loading, TC/TCX/XDP attachment, namespace traffic tests, systemd deployment, or USB/Wi-Fi/AF_XDP hardware tests. The environment lacks the BPF compiler target, bpftool, libbpf development files, and Meson needed for those checks. C++ integration code that depends on generated skeletons/libbpf was reviewed against API definitions but not compiled. No ThreadSanitizer result or performance claim is implied by the host tests above.

These documents are an implementation handoff, not a patch claiming all integration functions exist. “Complete file” labels identify self-contained proposed source files; manager orchestration, active control-plane publication, CLI integration, and AF_XDP ownership fragments still need to be assembled and reviewed in the repository. The upstream repository has not been modified.

## References

[build]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/meson.build
[testrun]: https://docs.kernel.org/bpf/bpf_prog_run.html
[offloads]: https://docs.kernel.org/networking/segmentation-offloads.html
[caps]: https://man7.org/linux/man-pages/man7/capabilities.7.html

The kernel documents describe test-run and skb/offload semantics. The pinned Meson file is the original test integration being replaced.
---

[09 — Application and capture integration](09_application_and_capture_integration.md) · End of series
