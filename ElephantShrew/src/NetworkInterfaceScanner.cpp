#include <iostream>
#include <spdlog/spdlog.h>
#include <pcapplusplus/NetworkUtils.h>
#include <pcapplusplus/IpAddress.h>
#include <pcapplusplus/PcapLiveDeviceList.h>
#include <NetworkInterfaceScanner.hpp>
#include <boost/redis.hpp>
#include <boost/redis/connection.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/consign.hpp>
#include <iostream>

#if defined(BOOST_ASIO_HAS_CO_AWAIT)
 
namespace asio = boost::asio;
using boost::redis::request;
using boost::redis::response;
using boost::redis::config;
using boost::redis::connection;
 
// Called from the main function (see main.cpp)
auto co_main(config cfg) -> asio::awaitable<void>
{
   auto conn = std::make_shared<connection>(co_await asio::this_coro::executor);
   conn->async_run(cfg, {}, asio::consign(asio::detached, conn));
 
   // A request containing only a ping command.
   request req;
   req.push("PING", "Hello world");
 
   // Response where the PONG response will be stored.
   response<std::string> resp;
 
   // Executes the request.
   co_await conn->async_exec(req, resp, asio::deferred);
   conn->cancel();
 
   std::cout << "PING: " << std::get<0>(resp).value() << std::endl;
}
 
#endif // defined(BOOST_ASIO_HAS_CO_AWAIT)

namespace ElephantShrew {

void NetworkInterfaceScanner::Scan() {

    // Get the list of network interfaces
    auto devList = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
    
    // if (devList == nullptr || devList->empty()) {
    //     std::cerr << "No network interfaces found.\n";
    //     return;
    // }

    for (auto* dev : devList) {
        // Display interface name and IP addresses
        spdlog::info("Interface: " + dev->getName());

        // Check if the device has IPv4 address
        if (dev->getIPv4Address() != pcpp::IPv4Address::Zero) {
            spdlog::info("  IPv4 Address: " + dev->getIPv4Address().toString());
        }

        // Check if the device has IPv6 address
        if (dev->getIPv6Address() != pcpp::IPv6Address::Zero) {
            spdlog::info("  IPv6 Address: " + dev->getIPv6Address().toString());
        }

        spdlog::info("  Mac Address: " + dev->getMacAddress().toString());
        spdlog::info("  Type: " + dev->getDeviceType());
    }
}

int NetworkInterfaceScanner::SavePackets() {


    return 0;
}

}