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
