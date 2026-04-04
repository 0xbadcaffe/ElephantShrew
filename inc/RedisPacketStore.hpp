#ifndef _REDISPACKETSTORE_H_
#define _REDISPACKETSTORE_H_

#include "IPacketStore.hpp"
#include "RuntimeConfig.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/redis/connection.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

namespace ElephantShrew {

class RedisPacketStore : public IPacketStore {
public:
    explicit RedisPacketStore(RedisStoreOptions options = {});
    ~RedisPacketStore() override;

    void Store(const PacketInfo& packet) override;

private:
    void ValidateConnection(std::chrono::milliseconds timeout);
    bool ReservePendingWriteSlot();
    void CompletePendingWrite();
    void WaitForPendingWrites(std::chrono::milliseconds timeout);

    boost::asio::io_context    ioc_;
    boost::redis::connection   conn_;
    std::thread                io_thread_;
    const RedisStoreOptions    options_;
    std::mutex                 pending_mutex_;
    std::condition_variable    pending_cv_;
    std::size_t                pending_writes_{0};
    bool                       accepting_writes_{true};
};

} // namespace ElephantShrew

#endif // _REDISPACKETSTORE_H_
