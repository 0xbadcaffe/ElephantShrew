#ifndef _IPACKETSTORE_H_
#define _IPACKETSTORE_H_

#include <cstdint>
#include <string>

namespace ElephantShrew {

struct PacketInfo {
    std::string iface;
    std::string src_ip;
    std::string dst_ip;
    std::string protocol;
    uint32_t    length{0};
    std::string data_hex;
    uint64_t    timestamp_us{0};
};

class IPacketStore {
public:
    virtual ~IPacketStore() = default;
    virtual void Store(const PacketInfo& packet) = 0;
};

} // namespace ElephantShrew

#endif // _IPACKETSTORE_H_
