# 06 — libbpf loader and attachment lifecycle

ElephantShrew eBPF implementation series · userspace ownership of kernel resources

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on 05. Open/load an object without attaching it, then attach to resolved interfaces with explicit ownership. Keep object loading, attachment choice, and telemetry consumption separate. This chapter supplies complete object-loader and legacy-attachment components; chapter 09 composes them into the service.

## File changes

```text
inc/
├── EbpfObject.hpp                    [ADD]
├── TcAttachment.hpp                  [ADD: header-only legacy owner]
├── EbpfCapabilities.hpp              [ADD]
└── EbpfManager.hpp                   [ADD interface in 09]
src/
├── EbpfObject.cpp                    [ADD]
├── EbpfCapabilities.cpp              [ADD]
└── EbpfManager.cpp                   [ASSEMBLE in 09]
```

## Object loading

Only validated options reach `LoadObserver`. Validation must enforce ring/page size and memory bounds, valid direction selection, and snapshot limits. The returned map FDs are borrowed from `owner`; keep that owner alive until attachments, ring consumers, and counter readers are finished.

### `inc/EbpfObject.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_EBPF_OBJECT_HPP
#define ELEPHANTSHREW_EBPF_OBJECT_HPP
#include <cstdint>
#include <memory>
struct bpf_program;
namespace ElephantShrew {
enum class TcApi { Legacy, Tcx };
struct ObjectOptions {
    std::uint32_t sample_every{128};
    std::uint32_t snapshot_bytes{0};
    std::uint32_t ring_buffer_bytes{8u * 1024u * 1024u};
    std::uint32_t max_flows{4096};
    std::uint8_t select_l4_proto{0};
    bool classify{true}, flow_enabled{false};
    bool ingress{true}, egress{true};
};
struct LoadedObserver {
    std::shared_ptr<void> owner;
    bpf_program* ingress{};
    bpf_program* egress{};
    int events_fd{-1}, counters_fd{-1}, flows_fd{-1};
};
LoadedObserver LoadObserver(const ObjectOptions& options, TcApi api);
} // namespace ElephantShrew
#endif
```

### `src/EbpfObject.cpp` — complete file

```cpp
#include "EbpfObject.hpp"
#include "es_build_config.h"
#include "es_tc_legacy.skel.h"
#if ES_HAS_TCX
#include "es_tc_tcx.skel.h"
#endif
#include <bpf/libbpf.h>
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace ElephantShrew {
namespace {
void Check(int rc, const char* what) {
    if (rc != 0) throw std::system_error(-rc, std::generic_category(), what);
}
template<class Skel, class Open, class Load, class Destroy>
LoadedObserver LoadTyped(const ObjectOptions& o, Open open, Load load, Destroy destroy) {
    std::shared_ptr<Skel> s(open(), destroy);
    if (!s) throw std::system_error(errno ? errno : ENOMEM,
                                  std::generic_category(), "open observer skeleton");
    s->rodata->sample_every = o.sample_every;
    s->rodata->snapshot_bytes = o.snapshot_bytes;
    s->rodata->classify = o.classify;
    s->rodata->flow_enabled = o.flow_enabled;
    s->rodata->select_l4_proto = o.select_l4_proto;
    Check(bpf_map__set_max_entries(s->maps.events, o.ring_buffer_bytes), "resize ring");
    Check(bpf_map__set_max_entries(s->maps.flows, o.flow_enabled ? o.max_flows : 1), "resize flows");
    Check(bpf_program__set_autoload(s->progs.es_ingress, o.ingress), "select ingress");
    Check(bpf_program__set_autoload(s->progs.es_egress, o.egress), "select egress");
    Check(load(s.get()), "load observer; inspect libbpf/verifier diagnostics");
    LoadedObserver result;
    result.owner = s;
    result.ingress = o.ingress ? s->progs.es_ingress : nullptr;
    result.egress = o.egress ? s->progs.es_egress : nullptr;
    result.events_fd = bpf_map__fd(s->maps.events);
    result.counters_fd = bpf_map__fd(s->maps.counters);
    result.flows_fd = bpf_map__fd(s->maps.flows);
    return result;
}
}
LoadedObserver LoadObserver(const ObjectOptions& o, TcApi api) {
    if (!o.ingress && !o.egress) throw std::invalid_argument("no TC direction selected");
    if (api == TcApi::Legacy)
        return LoadTyped<es_tc_legacy>(o, es_tc_legacy__open,
                                       es_tc_legacy__load, es_tc_legacy__destroy);
#if ES_HAS_TCX
    if (api == TcApi::Tcx)
        return LoadTyped<es_tc_tcx>(o, es_tc_tcx__open,
                                    es_tc_tcx__load, es_tc_tcx__destroy);
#endif
    throw std::runtime_error("requested TC attachment API was not built");
}
} // namespace ElephantShrew
```

Open, configure, load, then attach. Capture libbpf diagnostics on load failure and include interface, selected API, object role, errno, and verifier output in the error report. Do not turn a verifier rejection into an automatic pcap fallback for an active policy. Configure a bounded verifier log buffer when more detail is needed; logging must not throw across a C callback [overview].

## Legacy TC attachment

Before creating an attachment, inspect qdisc/filter state through rtnetlink and select an operator-approved handle/priority. The component below will not overwrite an occupied identity. Accepting `-EEXIST` from qdisc creation does not prove arbitrary pre-existing qdisc configuration is compatible; the manager must perform that inspection.

### `inc/TcAttachment.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_TC_ATTACHMENT_HPP
#define ELEPHANTSHREW_TC_ATTACHMENT_HPP
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <system_error>

namespace ElephantShrew {
class TcAttachment {
    bpf_tc_hook hook_{};
    std::uint32_t handle_{}, priority_{}, program_id_{};
    bool attached_{};
public:
    TcAttachment() = default;
    TcAttachment(const TcAttachment&) = delete;
    TcAttachment& operator=(const TcAttachment&) = delete;
    ~TcAttachment() noexcept {
        const int rc = Detach();
        if (rc != 0) std::fprintf(stderr, "TC cleanup failed: %d\n", rc);
    }
    void Attach(int ifindex, bpf_tc_attach_point direction, int prog_fd,
                std::uint32_t handle, std::uint32_t priority) {
        if (attached_) throw std::logic_error("TC attachment already active");
        if (ifindex <= 0 || prog_fd < 0 || handle == 0 || priority == 0 || priority > 65535)
            throw std::invalid_argument("invalid TC attachment identity");
        bpf_prog_info info{};
        std::uint32_t len = sizeof(info);
        if (bpf_obj_get_info_by_fd(prog_fd, &info, &len) < 0)
            throw std::system_error(errno, std::generic_category(), "read program ID");
        hook_ = {};
        hook_.sz = sizeof(hook_);
        hook_.ifindex = ifindex;
        hook_.attach_point = direction;
        int rc = bpf_tc_hook_create(&hook_);
        if (rc != 0 && rc != -EEXIST)
            throw std::system_error(-rc, std::generic_category(), "create clsact");
        bpf_tc_opts opts{};
        opts.sz = sizeof(opts);
        opts.prog_fd = prog_fd;
        opts.handle = handle;
        opts.priority = priority;
        // No replacement flag. An occupied identity is an error.
        rc = bpf_tc_attach(&hook_, &opts);
        if (rc != 0)
            throw std::system_error(-rc, std::generic_category(), "attach TC filter");
        handle_ = opts.handle;
        priority_ = opts.priority;
        program_id_ = info.id;
        attached_ = true; // No throwing operations after ownership becomes active.
    }
    int Detach() noexcept {
        if (!attached_) return 0;
        bpf_tc_opts query{};
        query.sz = sizeof(query);
        query.handle = handle_;
        query.priority = priority_;
        int rc = bpf_tc_query(&hook_, &query);
        if (rc == -ENOENT || rc == -ENODEV) { attached_ = false; return 0; }
        if (rc != 0) return rc;
        if (query.prog_id != program_id_) {
            attached_ = false; // The old program is no longer at our slot.
            return -ESTALE;    // Do not delete the new owner's program.
        }
        // Detach requires fresh opts: prog_fd/prog_id/flags must remain zero.
        bpf_tc_opts detach{};
        detach.sz = sizeof(detach);
        detach.handle = handle_;
        detach.priority = priority_;
        rc = bpf_tc_detach(&hook_, &detach);
        if (rc == 0 || rc == -ENOENT || rc == -ENODEV) {
            attached_ = false;
            return 0;
        }
        return rc;
    }
};
} // namespace ElephantShrew
#endif
```

Never call `bpf_tc_hook_destroy()` as generic cleanup: depending on the hook, it can flush filters or remove a shared qdisc. This implementation removes only the recorded filter and deliberately leaves an empty `clsact` if it created one. Log that conservative behavior instead of silently deleting another owner's networking configuration [netlink].

The query-then-detach check is **not atomic against another privileged writer**. A concurrent administrator could replace the slot between those operations. Coordinate a single management owner or require TCX for environments where this race cannot be tolerated. Do not advertise legacy cleanup as race-free.

Legacy attachments can outlive a crashed process. Write a runtime ownership journal containing boot ID, network-namespace identity, interface identity, direction, handle, priority, program ID, role, and session ID. Recovery compares the journal with current state; it must not flush a qdisc or remove a program just because its name starts with `es_`.

## TCX link ownership — integration fragment

Compile this path only when `ES_HAS_TCX` is true and load the TCX object variant. Retain every returned link in an RAII owner. Do not pin links in the initial implementation.

```cpp
struct LinkDelete {
    void operator()(bpf_link* link) const noexcept {
        if (link) {
            const int rc = bpf_link__destroy(link);
            if (rc != 0) std::fprintf(stderr, "BPF link cleanup failed: %d\n", rc);
        }
    }
};
using LinkPtr = std::unique_ptr<bpf_link, LinkDelete>;

LinkPtr AttachTcx(bpf_program* program, int ifindex) {
    bpf_tcx_opts options{};
    options.sz = sizeof(options);
    bpf_link* raw = bpf_program__attach_tcx(program, ifindex, &options);
    const long error = libbpf_get_error(raw);
    if (!raw || error)
        throw std::system_error(error ? static_cast<int>(-error) : errno,
                                std::generic_category(), "attach TCX");
    return LinkPtr(raw);
}
```

Keep ingress and egress links distinct. Free links before the object and never keep a raw link pointer after destruction. Link pinning, FD passing, or other surviving references change lifetime. For coexistence requiring a particular position, query the TCX revision and use supported relative/revision options; a default append is not a guarantee that the observer sees packets consumed by earlier programs [api].

## Capability reporting

### `inc/EbpfCapabilities.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_EBPF_CAPABILITIES_HPP
#define ELEPHANTSHREW_EBPF_CAPABILITIES_HPP
#include <string>
#include <vector>
namespace ElephantShrew {
struct CapabilityResult {
    std::string name;
    int probe_result; // 1 supported; 0 unavailable/not demonstrated; <0 probe error.
};
std::vector<CapabilityResult> ProbeEbpfCapabilities();
}
#endif
```

### `src/EbpfCapabilities.cpp` — complete file

```cpp
#include "EbpfCapabilities.hpp"
#include <bpf/libbpf.h>
namespace ElephantShrew {
std::vector<CapabilityResult> ProbeEbpfCapabilities() {
    return {
        {"sched_cls", libbpf_probe_bpf_prog_type(BPF_PROG_TYPE_SCHED_CLS, nullptr)},
        {"ringbuf", libbpf_probe_bpf_map_type(BPF_MAP_TYPE_RINGBUF, nullptr)},
        {"percpu_array", libbpf_probe_bpf_map_type(BPF_MAP_TYPE_PERCPU_ARRAY, nullptr)},
        {"lru_percpu_hash", libbpf_probe_bpf_map_type(BPF_MAP_TYPE_LRU_PERCPU_HASH, nullptr)},
        {"tc_ringbuf_reserve", libbpf_probe_bpf_helper(BPF_PROG_TYPE_SCHED_CLS,
                                           BPF_FUNC_ringbuf_reserve, nullptr)},
        {"tc_skb_load_bytes", libbpf_probe_bpf_helper(BPF_PROG_TYPE_SCHED_CLS,
                                           BPF_FUNC_skb_load_bytes, nullptr)}
    };
}
}
```

These probes are advisory. Report zero as “not demonstrated/unavailable” and a negative result as a probe error; retain permission context rather than asserting the kernel lacks a feature. A successful program-type probe does not prove the complete object passes the verifier or attaches to the requested NIC. Report TCX attachment and native-XDP/zero-copy support as “not tested” until a controlled test establishes them [api].

Resolve names with `if_nametoindex()` only after entering the intended network namespace. Hold an FD for that namespace and obtain its identity with `fstat`. Reject duplicate indices, non-Ethernet interfaces, and missing devices. Track deletion/recreation with rtnetlink; a reused name is not the same device instance. Interface probing must not change the default route or management interface.

## Startup transaction

Perform the following operations under one manager-controlled lifecycle lock: validate/probe, allocate stable interface contexts, load every selected object, prepare all map consumers, start the poller, and attach each producer. Store ownership immediately after each successful attachment. A later failure unwinds only successful steps in reverse order.

For `attach_api=auto`, choose a supported path **before loading its object**. Limit fallback to passive mode with an explicit compatibility policy; do not catch all failures and retry legacy, because permission denial, verifier rejection, and an occupied attachment are not missing-TCX evidence. An explicit `tcx` requirement must remain an error if unavailable.

## Shutdown contract

Stop configuration changes; detach owned producers; collect final counters while maps are alive; drain events to a deadline; stop and join the ring reader; stop/cancel the sink worker; free the ring manager; release interface contexts and object owners. A detached legacy filter that failed to clean up may retain map references: record that as an operational cleanup failure.

A ring-poll wait timeout alone does not bound a call while callbacks continuously arrive. Chapter 07's callback checks an abort flag. A storage timeout likewise does not cancel an arbitrary blocked thread; sinks must provide cooperative cancellation or live behind a process boundary.

## Acceptance gate

Failure after the first successful attachment removes that attachment. Explicit shutdown is idempotent. Occupied identities fail without replacement. A TCX crash test releases unpinned links; a legacy crash test detects surviving owned filters and exercises recovery. No unrelated filter or shared qdisc is removed.

## References

[overview]: https://docs.kernel.org/bpf/libbpf/libbpf_overview.html
[api]: https://github.com/libbpf/libbpf/blob/master/src/libbpf.h
[netlink]: https://github.com/libbpf/libbpf/blob/master/src/netlink.c

Primary API contracts: [object lifecycle][overview], [libbpf attachment/probe API][api], and [legacy TC option/cleanup checks][netlink].
---

[05 — Meson and the BPF build pipeline](05_meson_bpf_build_pipeline.md) · [07 — Telemetry queues, counters, and sinks](07_telemetry_queues_counters_and_sinks.md)
