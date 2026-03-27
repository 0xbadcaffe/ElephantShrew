// This translation unit provides the non-template Boost.Redis implementations.
#include <boost/redis/src.hpp>

#include "RedisPacketStore.hpp"
#include <boost/asio/consign.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <spdlog/spdlog.h>
#include <exception>
#include <future>
#include <stdexcept>

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

    try {
        ValidateConnection(host, port);
        spdlog::info("RedisPacketStore connected to {}:{}", host, port);
    }
    catch (...) {
        conn_.cancel();
        ioc_.stop();
        if (io_thread_.joinable())
            io_thread_.join();
        throw;
    }
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
    try {
        boost::asio::post(ioc_, [this, packet] {
            try {
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
            }
            catch (const std::exception& e) {
                spdlog::error("RedisPacketStore::Store failed: {}", e.what());
            }
            catch (...) {
                spdlog::error("RedisPacketStore::Store failed with unknown exception");
            }
        });
    }
    catch (const std::exception& e) {
        spdlog::error("RedisPacketStore::Store post failed: {}", e.what());
    }
    catch (...) {
        spdlog::error("RedisPacketStore::Store post failed with unknown exception");
    }
}

void RedisPacketStore::ValidateConnection(const std::string& host,
                                          const std::string& port,
                                          std::chrono::milliseconds timeout)
{
    auto ready = std::make_shared<std::promise<void>>();
    auto future = ready->get_future();

    boost::asio::post(ioc_, [this, ready, host, port] {
        try {
            auto req  = std::make_shared<boost::redis::request>();
            auto resp = std::make_shared<boost::redis::response<std::string>>();

            req->push("PING");
            conn_.async_exec(*req, *resp,
                [ready, req, resp, host, port](boost::system::error_code ec, std::size_t) {
                    try {
                        if (ec) {
                            throw std::runtime_error(
                                "RedisPacketStore failed to connect to " + host + ":" + port + ": " + ec.message()
                            );
                        }

                        const auto reply = std::get<0>(*resp).value();
                        if (reply != "PONG")
                            throw std::runtime_error("RedisPacketStore received unexpected PING reply: " + reply);

                        ready->set_value();
                    }
                    catch (...) {
                        ready->set_exception(std::current_exception());
                    }
                }
            );
        }
        catch (...) {
            ready->set_exception(std::current_exception());
        }
    });

    if (future.wait_for(timeout) != std::future_status::ready) {
        throw std::runtime_error(
            "RedisPacketStore timed out connecting to " + host + ":" + port
        );
    }

    future.get();
}

} // namespace ElephantShrew
