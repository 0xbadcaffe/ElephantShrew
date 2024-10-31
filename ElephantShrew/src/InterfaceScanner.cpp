#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <string>

void listNetworkInterfaces() {
    struct ifaddrs* ifAddrStruct = nullptr;
    struct ifaddrs* ifa = nullptr;

    // Retrieve the list of network interfaces
    if (getifaddrs(&ifAddrStruct) == -1) {
        std::cerr << "Error getting network interfaces: " << strerror(errno) << std::endl;
        return;
    }

    // Loop through each network interface
    for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;

        // Interface name
        std::string interfaceName = ifa->ifa_name;
        std::cout << "Interface: " << interfaceName << std::endl;

        // Check if the address is IPv4
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char ipAddress[INET_ADDRSTRLEN];
            struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            inet_ntop(AF_INET, &sa->sin_addr, ipAddress, INET_ADDRSTRLEN);
            std::cout << "  IPv4 Address: " << ipAddress << std::endl;
        }
        // Check if the address is IPv6
        else if (ifa->ifa_addr->sa_family == AF_INET6) {
            char ipAddress[INET6_ADDRSTRLEN];
            struct sockaddr_in6* sa = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
            inet_ntop(AF_INET6, &sa->sin6_addr, ipAddress, INET6_ADDRSTRLEN);
            std::cout << "  IPv6 Address: " << ipAddress << std::endl;
        }

        // Additional flags, such as checking if the interface is up
        if (ifa->ifa_flags & IFF_UP) {
            std::cout << "  Status: UP" << std::endl;
        } else {
            std::cout << "  Status: DOWN" << std::endl;
        }

        // Check if the interface is loopback
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            std::cout << "  Type: Loopback" << std::endl;
        } else {
            std::cout << "  Type: Physical" << std::endl;
        }
    }

    // Free the linked list
    freeifaddrs(ifAddrStruct);
}
