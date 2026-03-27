// This translation unit provides the non-template Boost.Redis implementations.
#include <boost/redis/src.hpp>

#include "RedisPacketStore.hpp"
#include <boost/asio/consign.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <spdlog/spdlog.h>

namespace ElephantShrew {

RedisPacketStore::RedisPacketStore(const std::string& host, const std::string& port)
    : conn_(ioc_)
{
    boost::redis::config cfg;
    cfg.addr.host = host;
    cfg.addr.port = port;

    // Schedule async_run on the ioc before starting the thread so ioc_.run()
    // finds work immediately and does not return prematurely.
    conn_.async_run(cfg, {}, boost::asio::detached);

    io_thread_ = std::thread([this] { ioc_.run(); });

    spdlog::info("RedisPacketStore connecting to {}:{}", host, port);
}

RedisPacketStore::~RedisPacketStore()
{
    conn_.cancel();
    ioc_.stop();
    if (io_thread_.joinable())
        io_thread_.join();
}

void RedisPacketStore::Store(const PacketInfo& packet)
{
    boost::asio::post(ioc_, [this, packet] {
        auto req  = std::make_shared<boost::redis::request>();
        auto resp = std::make_shared<boost::redis::response<std::string>>();

        req->push("XADD", stream_key_, "*",
            "iface",        packet.iface,
            "src_ip",       packet.src_ip,
            "dst_ip",       packet.dst_ip,
            "protocol",     packet.protocol,
            "length",       std::to_string(packet.length),
            "timestamp_us", std::to_string(packet.timestamp_us),
            "data",         packet.data_hex
        );

        conn_.async_exec(*req, *resp,
            [req, resp](boost::system::error_code ec, std::size_t) {
                if (ec)
                    spdlog::error("Redis XADD failed: {}", ec.message());
            }
        );
    });
}

} // namespace ElephantShrew
