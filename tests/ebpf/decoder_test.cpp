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
