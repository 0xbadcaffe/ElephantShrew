#ifndef _REDISPACKETSTORE_H_
#define _REDISPACKETSTORE_H_

#include "IPacketStore.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/redis/connection.hpp>
#include <chrono>
#include <thread>
#include <string>

namespace ElephantShrew {

class RedisPacketStore : public IPacketStore {
public:
    explicit RedisPacketStore(const std::string& host = "127.0.0.1",
                              const std::string& port = "6379");
    ~RedisPacketStore() override;

    void Store(const PacketInfo& packet) override;

private:
    void ValidateConnection(const std::string& host,
                            const std::string& port,
                            std::chrono::milliseconds timeout = std::chrono::seconds(5));

    boost::asio::io_context    ioc_;
    boost::redis::connection   conn_;
    std::thread                io_thread_;
    const std::string          stream_key_{"elephantshrew:packets"};
};

} // namespace ElephantShrew

#endif // _REDISPACKETSTORE_H_
