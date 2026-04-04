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
#include <utility>

namespace ElephantShrew {

RedisPacketStore::RedisPacketStore(RedisStoreOptions options)
    : conn_(ioc_),
      options_(std::move(options))
{
    if (options_.host.empty())
        throw std::runtime_error("RedisPacketStore: host must not be empty");
    if (options_.port.empty())
        throw std::runtime_error("RedisPacketStore: port must not be empty");
    if (options_.stream_key.empty())
        throw std::runtime_error("RedisPacketStore: stream_key must not be empty");
    if (options_.max_pending_writes == 0)
        throw std::runtime_error("RedisPacketStore: max_pending_writes must be greater than 0");
    if (options_.connect_timeout_ms == 0)
        throw std::runtime_error("RedisPacketStore: connect_timeout_ms must be greater than 0");
    if (options_.drain_timeout_ms == 0)
        throw std::runtime_error("RedisPacketStore: drain_timeout_ms must be greater than 0");

    boost::redis::config cfg;
    cfg.addr.host = options_.host;
    cfg.addr.port = options_.port;

    // Schedule async_run on the ioc before starting the thread so ioc_.run()
    // finds work immediately and does not return prematurely.
    conn_.async_run(cfg, {}, boost::asio::detached);

    io_thread_ = std::thread([this] { ioc_.run(); });

    spdlog::info("RedisPacketStore connecting to {}:{}", options_.host, options_.port);

    try {
        ValidateConnection(std::chrono::milliseconds(options_.connect_timeout_ms));
        spdlog::info("RedisPacketStore connected to {}:{}", options_.host, options_.port);
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
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        accepting_writes_ = false;
    }
    pending_cv_.notify_all();
    WaitForPendingWrites(std::chrono::milliseconds(options_.drain_timeout_ms));

    conn_.cancel();
    ioc_.stop();
    if (io_thread_.joinable())
        io_thread_.join();
}

void RedisPacketStore::Store(const PacketInfo& packet)
{
    if (!ReservePendingWriteSlot()) {
        spdlog::warn("RedisPacketStore: dropping packet while store is shutting down");
        return;
    }

    try {
        boost::asio::post(ioc_, [this, packet] {
            try {
                auto req  = std::make_shared<boost::redis::request>();
                auto resp = std::make_shared<boost::redis::response<std::string>>();

                req->push("XADD", options_.stream_key, "*",
                    "iface",        packet.iface,
                    "src_ip",       packet.src_ip,
                    "dst_ip",       packet.dst_ip,
                    "protocol",     packet.protocol,
                    "length",       std::to_string(packet.length),
                    "timestamp_us", std::to_string(packet.timestamp_us),
                    "data",         packet.data_hex
                );

                conn_.async_exec(*req, *resp,
                    [this, req, resp](boost::system::error_code ec, std::size_t) {
                        if (ec)
                            spdlog::error("Redis XADD failed: {}", ec.message());
                        CompletePendingWrite();
                    }
                );
            }
            catch (const std::exception& e) {
                spdlog::error("RedisPacketStore::Store failed: {}", e.what());
                CompletePendingWrite();
            }
            catch (...) {
                spdlog::error("RedisPacketStore::Store failed with unknown exception");
                CompletePendingWrite();
            }
        });
    }
    catch (const std::exception& e) {
        spdlog::error("RedisPacketStore::Store post failed: {}", e.what());
        CompletePendingWrite();
    }
    catch (...) {
        spdlog::error("RedisPacketStore::Store post failed with unknown exception");
        CompletePendingWrite();
    }
}

void RedisPacketStore::ValidateConnection(std::chrono::milliseconds timeout)
{
    auto ready = std::make_shared<std::promise<void>>();
    auto future = ready->get_future();

    boost::asio::post(ioc_, [this, ready] {
        try {
            auto req  = std::make_shared<boost::redis::request>();
            auto resp = std::make_shared<boost::redis::response<std::string>>();

            req->push("PING");
            conn_.async_exec(*req, *resp,
                [this, ready, req, resp](boost::system::error_code ec, std::size_t) {
                    try {
                        if (ec) {
                            throw std::runtime_error(
                                "RedisPacketStore failed to connect to " + options_.host + ":" +
                                options_.port + ": " + ec.message()
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
            "RedisPacketStore timed out connecting to " + options_.host + ":" + options_.port
        );
    }

    future.get();
}

bool RedisPacketStore::ReservePendingWriteSlot()
{
    std::unique_lock<std::mutex> lock(pending_mutex_);
    pending_cv_.wait(lock, [this] {
        return pending_writes_ < options_.max_pending_writes || !accepting_writes_;
    });

    if (!accepting_writes_)
        return false;

    ++pending_writes_;
    return true;
}

void RedisPacketStore::CompletePendingWrite()
{
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (pending_writes_ > 0)
            --pending_writes_;
    }
    pending_cv_.notify_all();
}

void RedisPacketStore::WaitForPendingWrites(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(pending_mutex_);
    const bool drained = pending_cv_.wait_for(lock, timeout, [this] {
        return pending_writes_ == 0;
    });

    if (!drained) {
        spdlog::warn("RedisPacketStore: timed out draining {} pending writes during shutdown",
                     pending_writes_);
    }
}

} // namespace ElephantShrew
