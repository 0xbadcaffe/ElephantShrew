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
