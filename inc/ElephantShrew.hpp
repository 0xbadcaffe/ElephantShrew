#ifndef _ELEPHENTSHREW_H_
#define _ELEPHENTSHREW_H_

#include <memory>
#include <string>
#include <vector>
#include "IElephantShrew.hpp"
#include "IReceiver.hpp"

namespace pcpp {
class PcapLiveDevice;
}

namespace ElephantShrew {

    class ElephantShrew : public IElephantShrew {

    public:

        explicit ElephantShrew() = default;
        explicit ElephantShrew(const ElephantShrew& other) = delete;
        ElephantShrew& operator=(const ElephantShrew& rhs) = delete;
        explicit ElephantShrew(ElephantShrew&& other) = delete;
        ElephantShrew& operator=(const ElephantShrew&& rhs) = delete;

        virtual void Init(const RuntimeConfig& config) override;

        virtual ~ElephantShrew() override;

    private:
        void CloseDevices() noexcept;

        // One receiver per active capture interface.
        std::vector<std::shared_ptr<IReceiver>> receivers_;
        std::vector<pcpp::PcapLiveDevice*> opened_devices_;
    };

}

#endif // _ELEPHENTSHREW_H_
