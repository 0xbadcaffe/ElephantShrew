#ifndef ELEPHANTSHREW_EBPF_CAPABILITIES_HPP
#define ELEPHANTSHREW_EBPF_CAPABILITIES_HPP
#include <string>
#include <vector>
namespace ElephantShrew {
struct CapabilityResult {
    std::string name;
    int probe_result; // 1 supported; 0 unavailable/not demonstrated; <0 probe error.
};
std::vector<CapabilityResult> ProbeEbpfCapabilities();
}
#endif
