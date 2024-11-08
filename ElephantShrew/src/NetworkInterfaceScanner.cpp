#include <iostream>
#include <pcapplusplus/NetworkUtils.h>
#include <pcapplusplus/IpAddress.h>
#include <pcapplusplus/PcapLiveDeviceList.h>
#include <NetworkInterfaceScanner.hpp>


namespace NetworkInterfaceScanner {

void NetworkInterfaceScanner::Scan() {

        // Get the list of network interfaces
    // auto devList = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
    
    // if (devList == nullptr || devList->empty()) {
    //     std::cerr << "No network interfaces found.\n";
    //     return;
    // }

    // for (auto* dev : *devList) {
    //     // Display interface name and IP addresses
    //     std::cout << "Interface: " << dev->getName() << '\n';

    //     // Check if the device has IPv4 address
    //     if (dev->getIPv4Address() != pcpp::IPv4Address::Zero) {
    //         std::cout << "  IPv4 Address: " << dev->getIPv4Address().toString() << '\n';
    //     }

    //     // Check if the device has IPv6 address
    //     if (dev->getIPv6Address() != pcpp::IPv6Address::Zero) {
    //         std::cout << "  IPv6 Address: " << dev->getIPv6Address().toString() << '\n';
    //     }

    //     // Print interface status (UP/DOWN)
    //     std::cout << "  Status: " << (dev->isUp() ? "UP" : "DOWN") << '\n';

    //     // Check if the device is loopback
    //     std::cout << "  Type: " << (dev->isLoopback() ? "Loopback" : "Physical") << '\n';
    // }
}

}