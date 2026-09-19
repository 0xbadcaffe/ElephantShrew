# 08 — Runtime configuration and CLI

ElephantShrew eBPF implementation series · operator-facing configuration and fail-fast validation

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on the feature contracts from 01–07. Parse configuration without side effects, apply explicit CLI overrides, validate the final combination, and only then resolve/load/attach runtime resources. Keep `-c` explicit; do not imply that a file in the working directory is automatically loaded.

## File changes

```text
inc/EbpfOptions.hpp                   [ADD]
inc/RuntimeConfig.hpp                 [EDIT]
src/RuntimeConfig.cpp                 [EDIT: strict parsing and final validation]
src/ElephantShrewMain.cpp             [EDIT: CLI merge and terminating commands]
configs/ebpf-observe.json             [ADD]
configs/ebpf-hybrid.json              [ADD]
tests/ebpf/config_test.cpp            [ADD cases from this chapter]
```

## Runtime option types

### `inc/EbpfOptions.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_EBPF_OPTIONS_HPP
#define ELEPHANTSHREW_EBPF_OPTIONS_HPP
#include <cstdint>
#include <string>
#include <vector>
namespace ElephantShrew {
struct EbpfOptions {
    bool enabled{false};
    bool required{true};
    std::string hook{"tc"};
    std::string attach_api{"legacy"};
    std::string mode{"observe"};
    std::vector<std::string> directions{"ingress", "egress"};
    std::uint32_t sample_every{128};
    std::uint32_t snapshot_bytes{0};
    std::uint32_t ring_buffer_bytes{8u * 1024u * 1024u};
    std::uint32_t event_queue_capacity{8192};
    std::uint32_t stats_interval_ms{1000};
    std::uint32_t drain_timeout_ms{5000};
    std::uint32_t max_flows{4096};
    std::uint32_t select_l4_proto{0};
    bool classify{true};
    bool flow_enabled{false};
};
struct BuildFeatures {
    bool ebpf{}, tcx{}, active_tc{}, xdp{}, af_xdp{};
};
}
#endif
```

In `inc/RuntimeConfig.hpp`, include `EbpfOptions.hpp`; add `std::string backend{"pcap"};` to `CaptureOptions`, `std::string engine{"pcap"};` to `RoutingOptions`, and `EbpfOptions ebpf;` to `RuntimeConfig`. Preserve every existing storage, supervisor, and UI field. Declare `ValidateEffectiveConfig(const RuntimeConfig&, const BuildFeatures&)`.

The strings above are validated enums at the configuration boundary. Convert them to typed internal selections before resource creation. They must not remain unchecked strings that choose arbitrary program names or paths.

## Fix baseline precedence before adding new flags

The baseline JSON loader enables routing when interface strings are nonempty. Remove that inference. `routing.enabled` is authoritative; the CLI `--route` explicitly sets it true. Example interface names must never accidentally activate forwarding. Validate after applying CLI changes, including same-interface routes supplied only on the command line [config], [main].

Stop accepting unknown keys silently. Recognize root objects `capture`, `routing`, `redis`, `supervisor`, `ui`, and `ebpf`; the existing `redis` object is retained for compatibility with the current storage implementation. Strictly validate nested keys too. Do not rename storage configuration as part of an unrelated eBPF change.

### Strict-key and integer parsing — integration fragment

```cpp
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
using json = nlohmann::json;

void CheckKeys(const json& object, std::string_view where,
               std::initializer_list<std::string_view> allowed) {
    if (!object.is_object()) throw std::invalid_argument(std::string(where) + " must be an object");
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool found = false;
        for (auto key : allowed) found = found || key == it.key();
        if (!found) throw std::invalid_argument(std::string(where) + "." + it.key() + " is unknown");
    }
}
std::uint32_t ReadU32(const json& value, std::string_view name) {
    std::uint64_t n;
    if (value.is_number_unsigned()) n = value.get<std::uint64_t>();
    else if (value.is_number_integer()) {
        const auto signed_n = value.get<std::int64_t>();
        if (signed_n < 0) throw std::invalid_argument(std::string(name) + " must not be negative");
        n = static_cast<std::uint64_t>(signed_n);
    } else throw std::invalid_argument(std::string(name) + " must be an integer");
    if (n > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(std::string(name) + " exceeds uint32 range");
    return static_cast<std::uint32_t>(n);
}
```

For booleans and strings, require the matching JSON type rather than numeric or string coercion. Populate every `EbpfOptions` member explicitly; use `ReadU32` for numeric fields so negative numbers cannot wrap into huge unsigned map sizes. Reject simultaneous `capture.interfaces` and its legacy `ifaces` alias rather than picking one silently.

The exact accepted eBPF keys for the core implementation are:

```text
enabled, required, hook, attach_api, mode, directions,
sample_every, snapshot_bytes, ring_buffer_bytes, event_queue_capacity,
stats_interval_ms, drain_timeout_ms, max_flows,
select_l4_proto, classify, flow_enabled
```

For `capture`, preserve `interfaces`/`ifaces`, `record_packets`, and `debug_packets`, and add `backend`. For `routing`, preserve the documented aliases only when unambiguous, and add `engine`. Extend strict parsing alongside each later policy/XDP/AF_XDP schema; unknown future keys should fail, not be ignored.

## Final merged validation

The following function adds backend/eBPF checks. Keep and call the existing storage/supervisor validations as a separate step; the early return when eBPF is disabled must not skip them.

```cpp
// Integration fragment for src/RuntimeConfig.cpp, after adding the fields below.
// Includes: <algorithm>, <set>, <stdexcept>, <string>, <unistd.h>.
void ValidateEffectiveConfig(const RuntimeConfig& c, const BuildFeatures& built) {
    const auto& e = c.ebpf;
    auto require = [](bool ok, const char* message) {
        if (!ok) throw std::invalid_argument(message);
    };
    const auto& backend = c.capture.backend;
    require(backend == "none" || backend == "pcap" || backend == "af_xdp", "invalid capture.backend");
    require(backend != "none" || (!c.capture.record_packets && !c.capture.debug_packets),
            "capture.backend=none cannot record/debug full packets");
    require(backend != "af_xdp" || built.af_xdp, "AF_XDP receiver is not integrated in this build");
    require(c.routing.engine == "pcap" || c.routing.engine == "tc" || c.routing.engine == "xdp",
            "invalid routing.engine");
    if (c.routing.enabled) {
        require(!c.routing.ingress_iface.empty() && !c.routing.egress_iface.empty(), "routing needs both interfaces");
        require(c.routing.ingress_iface != c.routing.egress_iface, "routing interfaces must differ");
        require(c.routing.engine != "pcap" || backend == "pcap", "pcap routing requires pcap capture backend");
        if (c.routing.engine != "pcap") {
            require(e.enabled && e.mode == "forward", "kernel routing needs explicit eBPF forward mode");
            require(e.hook == c.routing.engine, "routing engine must match eBPF hook");
        }
    }
    std::set<std::string> names;
    for (const auto& name : c.capture.ifaces)
        require(!name.empty() && names.insert(name).second, "empty or duplicate capture interface");
    if (!e.enabled) {
        require(backend != "af_xdp", "AF_XDP capture requires the XDP manager");
        return; // Full existing storage/supervisor validation still runs separately.
    }
    require(built.ebpf, "eBPF was requested but not built");
    require(!c.capture.ifaces.empty() || c.routing.enabled, "eBPF requires explicit interfaces");
    require(e.hook == "tc" || e.hook == "xdp", "invalid ebpf.hook");
    require(e.mode == "observe" || e.mode == "policy" || e.mode == "forward", "invalid ebpf.mode");
    require(e.attach_api == "legacy" || e.attach_api == "tcx" || e.attach_api == "auto", "invalid ebpf.attach_api");
    require(e.attach_api != "tcx" || built.tcx, "TCX API not built");
    require(!e.directions.empty() && e.directions.size() <= 2, "invalid direction list");
    std::set<std::string> dirs;
    for (const auto& direction : e.directions)
        require((direction == "ingress" || direction == "egress") && dirs.insert(direction).second,
                "unknown or duplicate direction");
    require(e.snapshot_bytes <= 128, "snapshot_bytes exceeds event ABI limit");
    require(e.select_l4_proto <= 255, "select_l4_proto exceeds 8 bits");
    require(e.event_queue_capacity > 0 && e.event_queue_capacity <= 1048576, "invalid event queue capacity");
    require(e.max_flows > 0 && e.max_flows <= 65536, "invalid flow capacity");
    require(e.stats_interval_ms > 0 && e.stats_interval_ms <= 60000, "invalid stats interval");
    require(e.drain_timeout_ms <= 60000, "drain timeout too large");
    const long page = sysconf(_SC_PAGESIZE);
    require(page > 0, "cannot determine page size");
    const auto ring = e.ring_buffer_bytes;
    require(ring > 0 && (ring & (ring - 1)) == 0 &&
            ring >= static_cast<unsigned long>(page) && ring % page == 0 &&
            ring <= 256u * 1024u * 1024u, "invalid ring size");
    if (e.mode == "policy") {
        require(e.hook == "tc" && built.active_tc, "active TC policy is not integrated");
        require(!c.routing.enabled, "initial policy mode must not also own forwarding");
    }
    if (e.mode == "forward") {
        require(c.routing.enabled && c.routing.engine != "pcap", "forward mode requires a kernel forwarding engine");
        require(e.required, "active forwarding cannot silently degrade to no forwarding");
        require(e.hook != "tc" || built.active_tc, "TC forwarding is not integrated");
    }
    if (e.hook == "xdp") {
        require(built.xdp || (backend == "af_xdp" && built.af_xdp), "XDP backend not integrated");
        require(dirs.size() == 1 && dirs.contains("ingress"), "XDP supports ingress only");
        // Chapter 04's minimal XDP object has counters, not TC feature parity.
        require(!e.classify && !e.flow_enabled && e.sample_every == 0 &&
                e.snapshot_bytes == 0 && e.select_l4_proto == 0,
                "minimal XDP object does not implement TC telemetry features");
    }
}
```

Add mode-specific validation before advertising active features: policy tables/defaults from 04; explicit XDP attach mode and failure behavior; AF_XDP queue/frame sizes and packet disposition from 09; topology/MTU/receive-mode checks at runtime. `BuildFeatures.active_tc/xdp/af_xdp` must stay false until those integrated branches exist, even when their experimental objects compile.

`--check-config` performs syntax, enum, combination, and size validation without attaching. Interface existence, permissions, exact helper/driver compatibility, possible-CPU memory accounting, and occupied attachment ownership belong to runtime preflight or `--ebpf-probe`. Keep the distinction visible in output.

## CLI changes

Use `std::optional<bool>` for overrideable booleans so absence differs from an explicit false. Retain `-i`, `-c`, `-r`, `-d`, `-s`, `--route`, and `--bidirectional`. Add `--no-record`, `--no-debug`, `--ebpf`, `--no-ebpf`, `--ebpf-hook`, `--ebpf-mode`, `--check-config`, `--ebpf-probe`, `--help`, and `--version`.

Integration fragment for `LoadEffectiveConfig`:

```cpp
// CliOptions fields shown here are new/changed; existing route parsing remains.
struct EbpfCliOverrides {
    std::optional<bool> enabled, record, debug;
    std::optional<std::string> hook, mode;
};
void ApplyOverrides(RuntimeConfig& c, const EbpfCliOverrides& o) {
    if (o.enabled) c.ebpf.enabled = *o.enabled;
    if (o.record) c.capture.record_packets = *o.record;
    if (o.debug) c.capture.debug_packets = *o.debug;
    if (o.hook) c.ebpf.hook = *o.hook;
    if (o.mode) c.ebpf.mode = *o.mode;
}
// main order:
// parse CLI -> early --help/--version -> load JSON -> apply overrides
// -> validate all effective config -> --check-config exit
// -> --ebpf-probe exit or runtime startup
```

Do not reject `--bidirectional` solely because `--route` was absent on the CLI when a valid route was provided by JSON. Instead validate the final routing configuration. Disallow contradictory terminating modes. Unknown flags and missing values must exit nonzero before any load or attach operation.

### `configs/ebpf-observe.json`

### `configs/ebpf-observe.json` — complete file

```json
{
  "capture": {
    "interfaces": [
      "es-left",
      "es-right"
    ],
    "backend": "none",
    "record_packets": false,
    "debug_packets": false
  },
  "routing": {
    "enabled": false,
    "engine": "pcap"
  },
  "ebpf": {
    "enabled": true,
    "hook": "tc",
    "attach_api": "legacy",
    "mode": "observe",
    "required": true,
    "directions": [
      "ingress",
      "egress"
    ],
    "sample_every": 128,
    "snapshot_bytes": 0,
    "ring_buffer_bytes": 8388608,
    "event_queue_capacity": 8192,
    "stats_interval_ms": 1000,
    "drain_timeout_ms": 5000,
    "classify": true,
    "flow_enabled": false,
    "max_flows": 4096,
    "select_l4_proto": 0
  },
  "ui": {
    "show_startup_art": false
  }
}
```

### Hybrid configuration

Create `configs/ebpf-hybrid.json` from the file above, set `capture.backend` to `pcap`, and set `record_packets` true. Keep the existing packet-store settings and ensure its service is available. This records full packets independently of sampled eBPF metadata; protocol selection in eBPF does not filter pcap delivery.


## Required negative tests

Test unknown keys; `"false"` instead of a boolean; negative/overflow/floating-point queue sizes; ring sizes that are not valid powers of two/page multiples; duplicate interfaces/directions; unsupported TCX; record-with-no-capture; egress-XDP; forwarding without explicit ownership; and an active policy that tries to degrade silently after attach failure.

Add precedence tests for JSON `enabled=true` plus `--no-ebpf`, JSON recording plus `--no-record`, JSON routing disabled with nonempty names, and a same-interface CLI route. Test `sample_every=0` as valid, not as an error.

## Acceptance gate

A valid `--check-config` and `--help` terminate without elevated privileges. Invalid merged configuration never reaches the loader. The startup summary prints requested/effective capture backend, hook, mode, attachment API, interfaces, sampling, and memory allocation without printing credentials.

## References

[config]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/RuntimeConfig.cpp
[main]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/ElephantShrewMain.cpp
[json]: https://json.nlohmann.me/api/basic_json/is_number_unsigned/

Source baseline: [JSON parser][config] and [CLI merge][main]. Numeric parsing uses [the JSON library's type inspection][json].
---

[07 — Telemetry queues, counters, and sinks](07_telemetry_queues_counters_and_sinks.md) · [09 — Application and capture integration](09_application_and_capture_integration.md)
