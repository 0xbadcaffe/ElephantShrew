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
