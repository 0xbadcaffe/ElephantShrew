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
