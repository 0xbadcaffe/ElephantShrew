# 09 — Application and capture integration

ElephantShrew eBPF implementation series · service orchestration and capture backends

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.


## Objective

Connect the components from 01–08 to the existing service without changing the meaning of packet recording. The first integrated release supports `capture.backend=none|pcap` plus the passive TC observer. Active TC, XDP, and AF_XDP remain separately gated until their ownership, configuration, and integration tests pass.

## File structure changes

```text
inc/
  ElephantShrew.hpp                MODIFY: explicit service ownership and Tick/Stop
  Bootstrapper.hpp                 MODIFY: propagate Tick to the live service
  EbpfManager.hpp                  ADD: composition and lifecycle API
  AfXdpReceiver.hpp                LATER: separate packet receiver
src/
  ElephantShrew.cpp                MODIFY: prepare/start/stop selected components
  Bootstrapper.cpp                 MODIFY: Tick forwarding
  ElephantShrewMain.cpp            MODIFY: service health polling
  EbpfManager.cpp                  ADD: assemble 06 and 07 components
  PcapReceiver.cpp                 MODIFY: forwarding ownership and recording queue
  RedisPacketStore.cpp             MODIFY: cancellation before receiver shutdown
  AfXdpReceiver.cpp                LATER: UMEM, queues, RX ownership, cleanup
int/
  IPacketStore.hpp                 MODIFY: explicit StopAccepting contract
  IReceiver.hpp                    LATER: explicit Stop contract for all receivers
bpf/
  elephantshrew_xsk.bpf.c           LATER: XSK map dispatcher, not passive capture
```

The existing bootstrapper retains a `shared_ptr<ElephantShrew>` after `Init`. Keep this ownership rather than starting a detached observer thread whose lifetime outlives the service. The existing application's interface handles and receivers are separate resources; closing the packet-capture handles is not an eBPF detach operation. [service], [bootstrap]

## A. Define the manager boundary

The following is an **interface/integration fragment**, not a complete implementation. Use the concrete object, attachment, queue, and decoder components in 06–07 inside `Impl`.

```cpp
// inc/EbpfManager.hpp
#pragma once
#include <chrono>
#include <memory>
#include "EbpfOptions.hpp"
#include "ITelemetrySink.hpp"
namespace ElephantShrew {
struct RuntimeConfig;
class EbpfManager {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    EbpfManager();                  // Define out of line in EbpfManager.cpp.
    ~EbpfManager();                 // Calls Stop; no exceptions escape.
    EbpfManager(const EbpfManager&) = delete;
    EbpfManager& operator=(const EbpfManager&) = delete;

    // All-or-nothing for required observers; never silently enables active mode.
    void Start(const RuntimeConfig&, std::shared_ptr<ITelemetrySink>);
    // Drain a bounded number of events; schedule cumulative stats; check health.
    void Tick();
    // One global deadline for all interfaces and all drain operations.
    void Stop(std::chrono::steady_clock::time_point deadline) noexcept;
};
}
```

`Impl` owns the network-namespace file descriptor, startup/session ID, resolved interface identities, loaded observer objects, all attachment records, one `EbpfTelemetry`, and the sink. In the initial implementation, `Tick()` is the **single queue consumer**. The ring-reader thread is its single producer. A sink that needs its own I/O thread accepts owned records through another bounded queue; it never blocks `Tick()` on a network connection.

Define these private operations in `src/EbpfManager.cpp` before connecting it to Meson:

| Operation | Concrete responsibility |
|---|---|
| `ResolveTargets` | Use the requested namespace and names; reject duplicates, nonexistent targets, unsupported link types, and topology conflicts. |
| `PrepareObjects` | Build `ObjectOptions`, call `LoadObserver`, retain object owners, register all event maps with `EbpfTelemetry::AddSource`. |
| `AttachAll` | Start the ring reader first; attach each requested direction using `TcAttachment` or retained TCX links. Store ownership immediately. |
| `PublishReady` | Only after all requested attachments and sinks are ready, emit the `eBPF ready` log and effective-mode summary. |
| `Tick` | Bounded queue drain, scheduled counter reads, interface lifecycle checks, reader/sink health checks. |
| `DetachAll` | Reverse-order, ownership-checked detach. Keep maps alive even when an attachment error must be reported. |
| `Stop` | Detach, bounded ring drain, bounded queue drain, sink stop, then free readers and object owners. |

`Start` must catch every exception after the first owned resource exists, run the same idempotent cleanup path, and rethrow. Do not mark the service ready while only the first interface of a requested multi-interface set is running.

### Interface identity and namespace handling

Resolve `if_nametoindex()` in the intended namespace, retain a descriptor for that namespace, and attach a namespace identity and startup ID to every userspace record. Use `fstat()` on the namespace descriptor; record both device/inode in a deployment-level identity, rather than treating an inode alone as globally unique forever. The event wrapper in 07 stores its inode together with a session ID; extend persisted metadata when records from multiple boots/hosts are combined.

Check the interface hardware type through rtnetlink or `SIOCGIFHWADDR`; the parser in 02 requires Ethernet framing. Do not equate “has an interface index” with “delivers Ethernet frames.” Check existing attachment state before allocating legacy TC filter handles. Keep all namespace-sensitive setup and teardown in that namespace, or run the whole service with `ip netns exec` as the test in 10 does.

Use an `RTM_DELLINK`/`RTM_NEWLINK` listener for deletion/recreation. A periodic name lookup alone cannot reliably distinguish every reuse race. For the first release, **stop and report a removed/recreated target**, rather than automatically attaching to a new device that happens to reuse its name. Never redirect to a stale cached ifindex.

### `Tick()` — integration fragment

```cpp
// Members below live inside EbpfManager::Impl; report functions must be bounded.
void Tick() {
    const auto loss = telemetry->Loss();
    if (loss.poll_error != 0) {
        // Required passive mode: throw so the supervisor sees a failed service.
        // Optional passive mode: detach and publish degraded health explicitly.
        // Active mode never degrades to unprotected pcap operation.
        HandleReaderFailure(loss.poll_error);
    }

    TelemetryRecord record;
    for (unsigned int budget = 0; budget < 512 && telemetry->TryPop(record); ++budget) {
        if (!sink->TryPublish(record)) ++sink_queue_dropped;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_stats) {
        // Read cumulative per-CPU maps using the stride logic from chapter 07.
        // Read only a configured flow-scan budget, never an unbounded whole map.
        PublishCounterSnapshots();
        next_stats = now + stats_interval;
    }
    CheckInterfaceLifecycle();
}
```

The 512-record budget is a proposed scheduler limit, not a throughput guarantee. Make it configurable or adjust it from measurements. When the service loop cannot keep up, `telemetry_queue_dropped` must increase instead of permitting unbounded memory growth. Emit a periodic summary such as `eBPF stats iface=es-left direction=ingress packets=...`; do not log every event by default.

### Shutdown — orchestration pseudocode

```text
Stop(deadline):
    reject new control-plane changes
    cancel packet-store admission before any pcap callback join
    detach owned active programs and observer programs in reverse order
    stop/join pcap receivers; close their capture handles
    telemetry.FinishAfterDetach(time remaining until deadline)
    consume queued telemetry until empty or deadline; count rejected records
    sink.Stop(the same deadline)
    destroy ring-reader resources, then BPF objects/maps
    close namespace descriptor
    record detach failures and incomplete drain; do not claim a clean stop
```

A deadline is not permission to free memory still accessed by a worker. Every I/O worker must implement cancellation and bounded operations before a bounded stop can be promised. Do not detach a C++ thread to avoid joining it. For legacy TC, a failed detach can leave the program attached even after object FDs close; preserve ownership diagnostics for recovery. Never delete a shared qdisc to hide this failure.

An unpinned TCX link normally disappears when its last reference is closed. Legacy TC filter lifetime is different. Process death tests must cover both; `kill -9` cannot run a C++ destructor. [libbpf]

## B. Connect to the existing application

Add `packet_store_`, `telemetry_sink_`, and `ebpf_` as service-owned members. Keep eBPF types forward-declared where possible and guard optional code with `ES_HAS_EBPF` from the generated build header. Define the `ElephantShrew` constructor/destructor out of line when owning an incomplete `unique_ptr` type.

Integration fragment for `inc/ElephantShrew.hpp`:

```cpp
// Add forward declarations in the ElephantShrew namespace.
class EbpfManager;
class IPacketStore;
class ITelemetrySink;

// In class ElephantShrew, alongside the existing members:
// public:
//   ElephantShrew();              // replaces the inline = default constructor
//   void Tick();
//   void Stop() noexcept;
// private:
//   std::shared_ptr<IPacketStore> packet_store_;
//   std::shared_ptr<ITelemetrySink> telemetry_sink_;
//   std::unique_ptr<EbpfManager> ebpf_;   // conditional on ES_HAS_EBPF
```

Refactor `Init` into preparation and activation, rather than inserting a second routing branch into the existing capture loop. The target behavior is:

| Requested operation | Pcap handles | Pcap reinjection | eBPF |
|---|---|---|---|
| Existing pcap capture | Selected interfaces | Off | Off |
| Observe-only, `backend=none` | None | Off | Passive TC |
| Hybrid pcap + observation | Selected interfaces | Off | Passive TC |
| Existing userspace forwarding + observation | Route pair | Exactly the existing owner | Passive TC only |
| TC/XDP forwarding | Only an explicitly designed recording path | **Off** for the forwarded traffic | Exactly one active engine |
| AF_XDP processing | No independent duplicate receiver by default | Off | XSK dispatcher plus AF_XDP socket |

In the existing `PcapReceiver` constructor calls, set `route_device` only when `routing.enabled && routing.engine == "pcap"`. Do not simply leave the old `routing.enabled` branch intact: it currently both selects interfaces and configures userspace reinjection. Separate those two decisions. [service], [pcap]

A kernel redirect can consume the packet before another observer sees it. “Recording enabled” must describe a tested recording path, such as explicit TC cloning to a dedicated capture interface, not an assumption that an unrelated pcap socket will see every forwarded packet. A TC ingress attachment also does not enable promiscuous reception on an Ethernet NIC; validate/configure receive mode when forwarding frames addressed to other hosts, and restore only state the application owns.

### Fix packet-store shutdown ordering

Add an explicit admission-cancellation method to `int/IPacketStore.hpp` and implement it in every store. Keep the existing storage class/file names; this is a lifecycle fix, not a database migration.

```cpp
// int/IPacketStore.hpp: add alongside Store().
virtual void StopAccepting() noexcept = 0;

// inc/RedisPacketStore.hpp: add the override declaration.
// src/RedisPacketStore.cpp: implement using the existing fields.
void RedisPacketStore::StopAccepting() noexcept {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        accepting_writes_ = false;
    }
    pending_cv_.notify_all();
}

// Its destructor should call StopAccepting(), then perform its bounded drain.
// ElephantShrew::Stop must call this while retaining packet_store_, BEFORE
// stopping/joining callbacks that may be waiting in ReservePendingWriteSlot().
```

The current receiver callback can block in `Store()` before posting asynchronous storage I/O. Clearing the receiver vector can therefore wait on that callback before the last shared store reference is destroyed. Explicitly stopping admission breaks that shutdown dependency. It does **not** make normal recording nonblocking; that requires a bounded recording queue as a separate change. [store]

For nonblocking recording, copy packet bytes into owned storage while the pcap callback still owns the packet. Give each interface its own SPSC queue, or use a correctly synchronized bounded multi-producer queue. Do not connect four pcap callbacks to the single-producer queue from 07. Make packet-store queue drops distinct from telemetry queue drops. No ring-buffer event callback may call the blocking `Store()` path.

### Make worker health visible to the supervisor

The current main loop waits for a stop signal after successful initialization. Add `Tick` to the concrete bootstrapper and service; otherwise reader failure can leave the outer restart loop unaware that monitoring has stopped. [main]

```cpp
// inc/Bootstrapper.hpp: add public void Tick();
// src/Bootstrapper.cpp:
void Bootstrapper::Tick() {
    if (elephantShrew_) elephantShrew_->Tick();
}

// src/ElephantShrew.cpp:
void ElephantShrew::Tick() {
#if ES_HAS_EBPF
    if (ebpf_) ebpf_->Tick();
#endif
}

// src/ElephantShrewMain.cpp: inside RunCaptureService, AFTER Resolve(config),
// replace WaitUntilStopRequested(...) with this loop.
while (!StopRequested()) {
    bootstrapper.Tick();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config.supervisor.poll_interval_ms));
}
```

Retain the fully merged configuration validation before this loop. Ensure a thrown health failure unwinds the bootstrapper before retrying; active forwarding must not be restarted into an inconsistent topology without its declared failure policy. For the initial passive version, fail startup if `required=true` cannot be satisfied; optional degradation must be visible in logs/status.

## C. AF_XDP is a separate optional receiver

Implement this only after the TC observer and the existing recorder are stable and benchmarks justify a new capture backend. The following kernel dispatcher is complete, but the userspace sections are integration fragments for a later `AfXdpReceiver`. Merely compiling this object does not implement AF_XDP support.

### `bpf/elephantshrew_xsk.bpf.c` — complete file

```c
/* SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0-only) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} xsks SEC(".maps");
SEC("xdp")
int es_xsk(struct xdp_md *ctx) {
    __u32 queue = ctx->rx_queue_index;
    /* Unregistered queues continue to the normal stack. Registered queues
     * are redirected, not cloned: the AF_XDP consumer owns their disposition. */
    return bpf_redirect_map(&xsks, queue, XDP_PASS);
}
char LICENSE[] SEC("license") = "Dual BSD/GPL";
```

Select explicit queue IDs and validate that each socket is bound to the same interface and queue as the dispatcher key. Populate the fill ring before making a socket reachable through the XSK map. Keep other XDP program ownership intact; use the explicit attach/cleanup approach from 04 or a tested libxdp composition strategy. [afxdp], [xsk]

Start with **copy mode**, aligned 4096-byte UMEM chunks, zero configured headroom, and no multi-buffer support. These are implementation choices, not a guarantee that every driver/device can use the selected configuration. Reject unsupported MTUs and multi-buffer descriptors until that path exists.

Integration fragment for `src/AfXdpReceiver.cpp`:

```cpp
#include <xdp/xsk.h>
#include <linux/if_xdp.h>

// These are local configuration values, not a complete resource-owning class.
xsk_umem_config uc{};
uc.fill_size = 2048;
uc.comp_size = 2048;
uc.frame_size = 4096;
uc.frame_headroom = 0;
uc.flags = 0;                         // Aligned chunks; no unaligned mode.

xsk_socket_config sc{};
sc.rx_size = 2048;
sc.tx_size = 2048;                    // No TX ring is created when tx=nullptr.
sc.libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD;
sc.xdp_flags = 0;                     // Our attachment owner selects XDP mode.
sc.bind_flags = XDP_COPY | XDP_USE_NEED_WAKEUP;

// Allocate page-aligned UMEM, then:
// xsk_umem__create(&umem, region, region_size, &fill, &completion, &uc);
// xsk_socket__create(&socket, iface.c_str(), queue_id, umem, &rx, nullptr, &sc);
// Fill frames -> insert xsk_socket__fd(socket) into xsks[queue_id] -> attach.
// Check every return value; unwind sockets before UMEM before region memory.
```

Prevent xsk socket creation from implicitly loading an unrelated default XDP program. Do not request `XDP_ZEROCOPY` and silently report success in copy mode. A future zero-copy option must inspect the actual socket mode and report it. Native XDP attachment alone is not evidence of zero-copy operation. [afxdp], [xsk]

### Frame ownership

```text
free frame -> fill ring -> kernel RX -> RX descriptor
    -> copy into a bounded owned recording item -> local recycle list -> fill ring

future zero-copy consumer lease:
RX descriptor -> leased frame -> last consumer releases lease -> recycle list
```

Keep the recycle list bounded by the total number of UMEM frames. If the fill ring is temporarily full, retain frame addresses in that list rather than forgetting them. Do not wait indefinitely for ring space in the RX path. Service need-wakeup with bounded polling and check stop requests between batches.

Integration pseudocode for each received descriptor:

```text
peek a bounded RX batch
for each descriptor:
    reject unsupported multi-buffer continuation
    validate address and length against UMEM region and its aligned frame
    derive the frame base, not just the packet-data address
    try to copy bytes into an owned bounded recording item
    count recording rejection without leaking the UMEM frame
    enqueue frame base into the preallocated recycle list
release the RX descriptors
refill available fill-ring entries from the recycle list
perform bounded need-wakeup handling
```

Even with zero configured UMEM headroom, the packet data address is not a general substitute for the frame base. For aligned power-of-two chunks, derive/validate the base using the configured frame size; for unaligned mode, use its documented address helpers and a different validated implementation. Never return the frame to the fill ring while an asynchronous storage worker still holds a pointer into it.

On shutdown: stop publishing socket mappings; remove only owned XSK map entries; detach the owned dispatcher; stop/join the RX worker; release outstanding leases; delete sockets; delete/unregister UMEM; and free the region only after successful ownership release. `xsk_umem__delete()` can fail while resources remain in use. Treat that as an ownership bug, not an invitation to free the memory anyway. [xsk]

Add `Stop() noexcept` to `IReceiver` only when introducing this backend, and update **all** implementing classes, including `PcapReceiver` and `UdpReceiver`. A default no-op stop that leaves an AF_XDP worker running is not sufficient.

### Required AF_XDP configuration additions

Add and validate an explicit `af_xdp` object: queue IDs, frame count, frame size, copy/zero-copy requirement, poll timeout, and packet disposition. The RX-only dispatcher above **consumes registered traffic**; it is not a transparent passive tap and does not automatically reinject frames to the host stack. Reject a request that expects normal host delivery until a separately designed mirror/reinjection path exists. TX requires an additional completion/recycling ownership path, not simply enabling a nonzero TX ring.

## Acceptance gate

The passive implementation must start and stop repeatedly across two interfaces, leave other owners' filters intact, keep ordinary pcap behavior unchanged, and surface a dead ring reader through the supervisor. Required monitoring cannot become silently optional. A stalled store must not deadlock capture shutdown. Advanced backends remain disabled until their topology and buffer-ownership tests in 10 pass.

## References

[service]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/ElephantShrew.cpp
[bootstrap]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/Bootstrapper.cpp
[pcap]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/PcapReceiver.cpp
[store]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/RedisPacketStore.cpp
[main]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/ElephantShrewMain.cpp
[libbpf]: https://github.com/libbpf/libbpf/blob/master/src/libbpf.h
[afxdp]: https://docs.kernel.org/networking/af_xdp.html
[xsk]: https://github.com/xdp-project/xdp-tools/blob/main/headers/xdp/xsk.h

The pinned sources document existing behavior. The AF_XDP and libbpf references document API contracts, not completed ElephantShrew support.
---

[08 — Runtime configuration and CLI](08_runtime_config_and_cli.md) · [10 — Tests, deployment, and delivery](10_tests_deployment_and_delivery.md)
