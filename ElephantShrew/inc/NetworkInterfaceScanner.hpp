#ifndef _NETWORKINTERFACESCANNER_
#define _NETWORKINTERFACESCANNER_

namespace ElephantShrew {

class NetworkInterfaceScanner {

    public:

        NetworkInterfaceScanner() = default;
        explicit NetworkInterfaceScanner(const NetworkInterfaceScanner& other) = delete;
        NetworkInterfaceScanner& operator=(const NetworkInterfaceScanner& rhs) = delete;
        explicit NetworkInterfaceScanner(NetworkInterfaceScanner&& other) = delete;
        NetworkInterfaceScanner& operator=(const NetworkInterfaceScanner&& rhs) = delete;

        void Scan();
        int SavePackets();

        virtual ~NetworkInterfaceScanner() = default;

    };

}


#endif // _NETWORKINTERFACESCANNER_