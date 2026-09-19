# 07 — Telemetry queues, counters, and sinks

ElephantShrew eBPF implementation series · bounded userspace telemetry pipeline

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on 01, 05, and 06. Copy each ring record into owned memory, deliver it through a bounded queue, aggregate per-CPU counters, and export telemetry without using the full-packet recorder's blocking path.

Exactly one thread calls `ring_buffer__poll()` for all registered interfaces. That thread is the queue's single producer; one worker consumes. Do not instantiate one poller per interface against the same SPSC queue. A shutdown drain may take over producer ownership only after joining the original producer.

## File changes

```text
inc/
├── SpscQueue.hpp                      [ADD]
├── TelemetryTypes.hpp                 [ADD]
└── EbpfTelemetry.hpp                  [ADD]
int/ITelemetrySink.hpp                 [ADD]
src/EbpfTelemetry.cpp                  [ADD]
int/IPacketStore.hpp                   [KEEP separate]
```

## Fixed-capacity queue

### `inc/SpscQueue.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_SPSC_QUEUE_HPP
#define ELEPHANTSHREW_SPSC_QUEUE_HPP
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>
namespace ElephantShrew {
// Exactly one producer and one consumer. All allocation happens at construction.
template<class T> class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    std::vector<T> slots_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t Next(std::size_t i) const noexcept {
        return i + 1 == slots_.size() ? 0 : i + 1;
    }
public:
    explicit SpscQueue(std::size_t capacity) {
        if (capacity == 0 || capacity == std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("invalid SPSC capacity");
        slots_.resize(capacity + 1); // One sentinel slot distinguishes full/empty.
    }
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    bool TryPush(const T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = Next(head);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        slots_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }
    bool TryPop(T& value) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        value = slots_[tail];
        tail_.store(Next(tail), std::memory_order_release);
        return true;
    }
};
}
#endif
```

### `inc/TelemetryTypes.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_TELEMETRY_TYPES_HPP
#define ELEPHANTSHREW_TELEMETRY_TYPES_HPP
#include "elephantshrew_shared.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ElephantShrew {
struct TelemetryRecord {
    es_event event{};
    std::uint64_t netns_inode{};
    std::uint64_t session_id{};
};
inline bool DecodeEvent(const void* data, std::size_t size, es_event& out) noexcept {
    if (!data || size != sizeof(es_event)) return false;
    es_event e{};
    std::memcpy(&e, data, sizeof(e)); // No alignment assumptions about C callback data.
    if (e.abi_version != ES_ABI_VERSION || e.record_size != sizeof(e) ||
        e.ifindex == 0 || e.direction >= ES_DIRECTIONS ||
        (e.hook != ES_HOOK_TC && e.hook != ES_HOOK_XDP) ||
        e.link_type != ES_LINKTYPE_ETHERNET ||
        e.parse_status > ES_PARSE_SKIPPED ||
        e.captured_len > ES_SNAPSHOT_MAX || e.captured_len > e.original_len ||
        e.tuple.vlan_count > 2 || e.tuple.reserved != 0 ||
        (e.flags & ~(ES_E_PREFIX_TRUNCATED | ES_E_COPY_FAILED | ES_E_VLAN_STRIPPED)) ||
        (e.tuple.flags & ~(ES_T_L2_VALID | ES_T_ADDR_VALID | ES_T_PORTS_VALID | ES_T_FRAGMENT)))
        return false;
    if ((e.flags & ES_E_COPY_FAILED) && e.captured_len != 0) return false;
    if ((e.tuple.flags & ES_T_PORTS_VALID) &&
        (!(e.tuple.flags & ES_T_ADDR_VALID) ||
         (e.tuple.flags & ES_T_FRAGMENT) ||
         (e.tuple.l4_proto != 6 && e.tuple.l4_proto != 17))) return false;
    out = e;
    return true;
}
struct TelemetryLoss {
    std::uint64_t decode_errors{}, queue_dropped{}, shutdown_discarded{};
    int poll_error{};
};
}
#endif
```

### `int/ITelemetrySink.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_I_TELEMETRY_SINK_HPP
#define ELEPHANTSHREW_I_TELEMETRY_SINK_HPP
#include "TelemetryTypes.hpp"
#include <chrono>
namespace ElephantShrew {
class ITelemetrySink {
public:
    virtual ~ITelemetrySink() = default;
    // Must copy/enqueue owned data and return promptly. true != persisted.
    virtual bool TryPublish(const TelemetryRecord& record) noexcept = 0;
    // Implementation must cancel its I/O cooperatively before this deadline.
    // false reports records that could not be drained/persisted.
    virtual bool Stop(std::chrono::steady_clock::time_point deadline) noexcept = 0;
};
}
#endif
```

The sink's contract is stronger than “asynchronous API”: it must not wait for capacity. `TryPublish` returning true means accepted, not durable. Count rejected events separately. A worker backed by arbitrary blocking I/O cannot guarantee bounded shutdown merely because `join()` is preceded by a timer.

## Ring reader implementation

### `inc/EbpfTelemetry.hpp` — complete file

```cpp
#ifndef ELEPHANTSHREW_EBPF_TELEMETRY_HPP
#define ELEPHANTSHREW_EBPF_TELEMETRY_HPP
#include "TelemetryTypes.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
namespace ElephantShrew {
class EbpfTelemetry {
    struct Impl;
    std::unique_ptr<Impl> p_;
public:
    explicit EbpfTelemetry(std::size_t queue_capacity);
    ~EbpfTelemetry();
    EbpfTelemetry(const EbpfTelemetry&) = delete;
    EbpfTelemetry& operator=(const EbpfTelemetry&) = delete;
    void AddSource(int ring_fd, std::uint32_t ifindex,
                   std::uint64_t netns_inode, std::uint64_t session_id);
    void Start();
    // Producers must be detached first. Keep sources and map owners alive.
    int FinishAfterDetach(std::chrono::milliseconds drain_timeout) noexcept;
    bool TryPop(TelemetryRecord& record) noexcept;
    TelemetryLoss Loss() const noexcept;
};
}
#endif
```

### `src/EbpfTelemetry.cpp` — complete file

```cpp
#include "EbpfTelemetry.hpp"
#include "SpscQueue.hpp"
#include <bpf/libbpf.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>
namespace ElephantShrew {
namespace {
std::uint64_t NowNs() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct RingDelete { void operator()(ring_buffer* p) const noexcept { ring_buffer__free(p); } };
}
struct EbpfTelemetry::Impl {
    struct Source {
        Impl* parent;
        std::uint32_t ifindex;
        std::uint64_t netns_inode, session_id;
    };
    SpscQueue<TelemetryRecord> queue;
    std::vector<std::unique_ptr<Source>> sources;
    std::unique_ptr<ring_buffer, RingDelete> ring;
    std::thread poller;
    std::atomic<bool> abort{false};
    std::atomic<std::uint64_t> deadline_ns{0};
    std::atomic<std::uint64_t> decode_errors{0}, queue_dropped{0}, shutdown_discarded{0};
    std::atomic<int> poll_error{0};
    bool started{}, finished{}; // Lifecycle calls are serialized by the manager.
    explicit Impl(std::size_t capacity) : queue(capacity) {}
    static int OnEvent(void* context, void* data, std::size_t size) noexcept {
        auto& source = *static_cast<Source*>(context);
        auto& self = *source.parent;
        const auto deadline = self.deadline_ns.load(std::memory_order_relaxed);
        if (self.abort.load(std::memory_order_relaxed) || (deadline && NowNs() >= deadline)) {
            self.shutdown_discarded.fetch_add(1, std::memory_order_relaxed);
            return -ECANCELED; // Also bounds a busy poll call during shutdown.
        }
        TelemetryRecord record{};
        if (!DecodeEvent(data, size, record.event) || record.event.ifindex != source.ifindex) {
            self.decode_errors.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        record.netns_inode = source.netns_inode;
        record.session_id = source.session_id;
        if (!self.queue.TryPush(record))
            self.queue_dropped.fetch_add(1, std::memory_order_relaxed);
        return 0; // No formatting, database I/O, allocation, or waiting here.
    }
};
EbpfTelemetry::EbpfTelemetry(std::size_t capacity) : p_(std::make_unique<Impl>(capacity)) {}
EbpfTelemetry::~EbpfTelemetry() { FinishAfterDetach(std::chrono::milliseconds(0)); }
void EbpfTelemetry::AddSource(int fd, std::uint32_t ifindex,
                             std::uint64_t netns, std::uint64_t session) {
    if (p_->started || p_->finished || fd < 0 || ifindex == 0)
        throw std::invalid_argument("invalid ring source/lifecycle");
    // Allocate/store the context before registering its pointer with libbpf.
    p_->sources.push_back(std::make_unique<Impl::Source>(Impl::Source{p_.get(), ifindex, netns, session}));
    auto* source = p_->sources.back().get();
    int rc = 0;
    if (!p_->ring) {
        p_->ring.reset(ring_buffer__new(fd, Impl::OnEvent, source, nullptr));
        if (!p_->ring) rc = -(errno ? errno : ENOMEM);
    } else {
        rc = ring_buffer__add(p_->ring.get(), fd, Impl::OnEvent, source);
    }
    if (rc != 0) {
        p_->sources.pop_back();
        throw std::system_error(-rc, std::generic_category(), "register ring source");
    }
}
void EbpfTelemetry::Start() {
    if (!p_->ring || p_->started || p_->finished) throw std::logic_error("cannot start ring poller");
    p_->poller = std::thread([state = p_.get()] {
        while (!state->abort.load(std::memory_order_relaxed)) {
            int rc = ring_buffer__poll(state->ring.get(), 50);
            if (rc == -EINTR) continue;
            if (rc == -ECANCELED && state->abort.load(std::memory_order_relaxed)) break;
            if (rc < 0) { state->poll_error.store(rc); break; }
        }
    });
    p_->started = true;
}
int EbpfTelemetry::FinishAfterDetach(std::chrono::milliseconds timeout) noexcept {
    if (p_->finished) return p_->poll_error.load();
    p_->abort.store(true);
    if (p_->poller.joinable()) p_->poller.join(); // Caller must not be the poller itself.
    // Only this thread now produces queue records; the sink consumer may continue.
    auto ms = timeout.count();
    if (ms > 60000) ms = 60000;
    if (ms > 0 && p_->ring) {
        p_->deadline_ns.store(NowNs() + static_cast<std::uint64_t>(ms) * 1000000u);
        p_->abort.store(false);
        for (;;) {
            if (NowNs() >= p_->deadline_ns.load()) break;
            int rc = ring_buffer__consume(p_->ring.get());
            if (rc == -EINTR) continue;
            if (rc == -ECANCELED || rc == 0) break;
            if (rc < 0) { p_->poll_error.store(rc); break; }
        }
    }
    p_->abort.store(true);
    p_->finished = true;
    return p_->poll_error.load();
}
bool EbpfTelemetry::TryPop(TelemetryRecord& record) noexcept { return p_->queue.TryPop(record); }
TelemetryLoss EbpfTelemetry::Loss() const noexcept {
    return {p_->decode_errors.load(), p_->queue_dropped.load(),
            p_->shutdown_discarded.load(), p_->poll_error.load()};
}
}
```

The reader's destructor stops callbacks; it does not detach kernel programs. `EbpfManager` must detach first. Best-effort shutdown can discard the callback record that observes cancellation and can leave additional unread ring records. `shutdown_discarded` counts only observed callback cancellations, **not an exact count of all unread bytes/records**. Export `drain_complete=false` when the deadline is reached.

A callback error used to end consumption is a control signal, not a packet-drop verdict. Different ring/map instances have no single global event order. Enrich the record with boot ID, namespace, session, interface identity, source role, and optional wall-clock estimate in the worker, not the callback [ring-api], [ring].

## Aggregate per-CPU maps — integration fragment

Add this helper to `src/EbpfManager.cpp` or a dedicated counter reader. The value stride is rounded to eight bytes and multiplied by the number of **possible** CPUs [percpu].

```cpp
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cstddef>
#include <cstring>
#include <limits>
#include <system_error>
#include <vector>

es_counters ReadCounters(int fd, std::uint32_t direction) {
    const int cpus = libbpf_num_possible_cpus();
    if (cpus <= 0) throw std::runtime_error("cannot determine possible CPUs");
    constexpr std::size_t stride = (sizeof(es_counters) + 7u) & ~std::size_t(7u);
    if (static_cast<std::size_t>(cpus) > std::numeric_limits<std::size_t>::max() / stride)
        throw std::overflow_error("per-CPU buffer too large");
    std::vector<std::byte> bytes(stride * static_cast<std::size_t>(cpus));
    if (bpf_map_lookup_elem(fd, &direction, bytes.data()) < 0)
        throw std::system_error(errno, std::generic_category(), "read per-CPU counters");
    es_counters total{};
    for (int cpu = 0; cpu < cpus; ++cpu) {
        es_counters one{};
        std::memcpy(&one, bytes.data() + static_cast<std::size_t>(cpu) * stride, sizeof(one));
        for (unsigned i = 0; i < ES_C_MAX; ++i) total.v[i] += one.v[i];
    }
    return total;
}
```

Read cumulative values; do not reset a live map to obtain interval statistics. Compute deltas only within the same map/session identity. Label the read as an approximate multi-CPU snapshot, not an atomic snapshot across all fields and CPUs. Report observed skb counts separately from unique wire packets.

For flows, use the same stride rule with `es_flow_value`. A scan needs a time/work budget because concurrent insertions/evictions can disturb iteration. Emit a bounded snapshot with a timestamp and `scan_complete` flag; do not loop until a changing LRU map happens to stop changing. Aggregate nonzero first timestamps, maximum last timestamp, and per-CPU packet/byte values. Never sum repeated cumulative exports as new traffic.

## Clock domains and serialization

Keep `timestamp_mono_ns` as the authoritative event clock. To display an estimated wall time, bracket a `CLOCK_REALTIME` read with `CLOCK_MONOTONIC` reads, use the midpoint offset, and carry the sampling uncertainty. Recalibrate after suspend/resume or clock changes. Do not write raw monotonic nanoseconds into the existing `PacketInfo::timestamp_us` field.

A proposed serialized telemetry row is:

```json
{
  "schema_version": 1,
  "kind": "packet_metadata",
  "event_abi": 2,
  "clock": "monotonic_ns",
  "timestamp_mono_ns": 123456789000,
  "netns_inode": 4026533000,
  "session_id": 42,
  "ifindex": 12,
  "direction": "ingress",
  "hook": "tc",
  "original_len": 1500,
  "captured_len": 0,
  "sample_every": 128,
  "parse_status": "ok",
  "storage_status": "accepted_not_yet_durable"
}
```

Use a distinct `packet_prefix` kind when `captured_len > 0`; preserve captured/original lengths and stripped-VLAN flags. The fixed ABI buffer must not be treated as a full Ethernet recording when the data is truncated. Limit retention and access to sampled packet bytes just as for ordinary packet captures.

## Operational counters

Publish kernel observed/selected/submitted/ring-loss counters; user decode/queue/callback-cancellation counters; and sink accepted/rejected/persisted/write-error counters separately. Health output must say when counters are stale or the poller has failed. A silent worker exception must not leave the supervisor reporting “capture healthy.”

## Acceptance gate

Queue full returns immediately; a stalled sink cannot stall the callback. Invalid records are rejected without pointer retention or exceptions escaping C. FIFO order is preserved within the single queue. Counter reads use possible CPUs. Repeated start/stop and partial startup leave no callbacks referencing freed contexts. Shutdown under continuous traffic returns within the configured operational budget, subject to the documented cooperative-I/O contract.

## References

[ring-api]: https://github.com/libbpf/libbpf/blob/master/src/libbpf.h
[ring]: https://docs.kernel.org/bpf/ringbuf.html
[percpu]: https://docs.kernel.org/bpf/map_hash.html

Primary contracts: [ring consumer and per-CPU API][ring-api], [ring semantics][ring], and [per-CPU map behavior][percpu]. Queue and record designs are original implementation proposals.
---

[06 — libbpf loader and attachment lifecycle](06_libbpf_loader_and_attachment_lifecycle.md) · [08 — Runtime configuration and CLI](08_runtime_config_and_cli.md)
