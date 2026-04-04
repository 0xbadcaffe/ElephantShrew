#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <optional>
#include <pcapplusplus/PcapLiveDeviceList.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "Bootstrapper.hpp"

namespace {

std::atomic<bool> g_stopRequested{false};

struct CliOptions
{
    std::optional<std::string> config_path;
    std::vector<std::string> ifaces;
    std::optional<std::string> route_spec;
    bool record_packets{false};
    bool debug_packets{false};
    bool bidirectional_route{false};
    bool scan_only{false};
};

void PrintUsage(const char* program)
{
    std::cout
        << "Usage: " << program << " [-s] [-r] [-d] [-c <config.json>] [-i <iface>]...\n"
        << "       " << program << " [options] --route <ingress:egress> [--bidirectional]\n";
}

void PrintStartupArt()
{
    std::cout <<
R"(███████╗██╗     ███████╗██████╗ ██╗  ██╗ █████╗ ███╗   ██╗████████╗
██╔════╝██║     ██╔════╝██╔══██╗██║  ██║██╔══██╗████╗  ██║╚══██╔══╝
█████╗  ██║     █████╗  ██████╔╝███████║███████║██╔██╗ ██║   ██║
██╔══╝  ██║     ██╔══╝  ██╔═══╝ ██╔══██║██╔══██║██║╚██╗██║   ██║
███████╗███████╗███████╗██║     ██║  ██║██║  ██║██║ ╚████║   ██║
╚══════╝╚══════╝╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝

 ███████╗██╗  ██╗██████╗ ███████╗██╗    ██╗
 ██╔════╝██║  ██║██╔══██╗██╔════╝██║    ██║
 ███████╗███████║██████╔╝█████╗  ██║ █╗ ██║
 ╚════██║██╔══██║██╔══██╗██╔══╝  ██║███╗██║
 ███████║██║  ██║██║  ██║███████╗╚███╔███╔╝
 ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝ ╚══╝╚══╝
)";
    std::cout.flush();
}

int ListInterfaces()
{
    const auto& dev_list = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();

    if (dev_list.empty()) {
        std::cout << "No network interfaces found.\n";
        return 1;
    }

    std::cout << "Available interfaces:\n";
    for (const auto* dev : dev_list)
        std::cout << " - " << dev->getName() << '\n';

    return 0;
}

void RequestStop([[maybe_unused]] int signal)
{
    g_stopRequested.store(true);
}

void InstallSignalHandlers()
{
    std::signal(SIGINT, RequestStop);
    std::signal(SIGTERM, RequestStop);
}

bool StopRequested()
{
    return g_stopRequested.load();
}

bool ParseRouteSpec(const std::string& route_spec, std::string& ingress_iface, std::string& egress_iface)
{
    std::size_t separator_pos = route_spec.find("->");
    std::size_t separator_size = 2;

    if (separator_pos == std::string::npos) {
        separator_pos = route_spec.find(':');
        separator_size = 1;
    }

    if (separator_pos == std::string::npos) {
        separator_pos = route_spec.find(',');
        separator_size = 1;
    }

    if (separator_pos == std::string::npos)
        return false;

    ingress_iface = route_spec.substr(0, separator_pos);
    egress_iface = route_spec.substr(separator_pos + separator_size);
    return !ingress_iface.empty() && !egress_iface.empty();
}

bool ParseArguments(int argc, char* argv[], CliOptions& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-c") {
            if (i + 1 >= argc) {
                spdlog::error("Missing value for '{}'", arg);
                return false;
            }

            options.config_path = argv[++i];
            continue;
        }

        if (arg == "-i") {
            if (i + 1 >= argc) {
                spdlog::error("Missing value for '{}'", arg);
                return false;
            }

            options.ifaces.push_back(argv[++i]);
            continue;
        }

        if (arg == "--route") {
            if (i + 1 >= argc) {
                spdlog::error("Missing value for '{}'", arg);
                return false;
            }

            options.route_spec = argv[++i];
            continue;
        }

        if (arg == "--bidirectional") {
            options.bidirectional_route = true;
            continue;
        }

        if (arg == "-s") {
            options.scan_only = true;
            continue;
        }

        if (arg == "-r") {
            options.record_packets = true;
            continue;
        }

        if (arg == "-d") {
            options.debug_packets = true;
            continue;
        }

        spdlog::error("Unknown argument '{}'", arg);
        return false;
    }

    if (options.scan_only &&
        (!options.ifaces.empty() || options.route_spec || options.record_packets ||
         options.debug_packets || options.bidirectional_route)) {
        spdlog::error("'-s' cannot be combined with capture flags");
        return false;
    }

    if (options.route_spec && !options.ifaces.empty()) {
        spdlog::error("'-i' cannot be combined with '--route'");
        return false;
    }

    if (options.bidirectional_route && !options.route_spec) {
        spdlog::error("'--bidirectional' requires '--route'");
        return false;
    }

    return true;
}

ElephantShrew::RuntimeConfig LoadEffectiveConfig(const CliOptions& options)
{
    ElephantShrew::RuntimeConfig config;

    if (options.config_path)
        config = ElephantShrew::LoadRuntimeConfigFromJson(*options.config_path);

    if (!options.ifaces.empty())
        config.capture.ifaces = options.ifaces;

    if (options.record_packets)
        config.capture.record_packets = true;

    if (options.debug_packets)
        config.capture.debug_packets = true;

    if (options.route_spec) {
        std::string ingress_iface;
        std::string egress_iface;
        if (!ParseRouteSpec(*options.route_spec, ingress_iface, egress_iface)) {
            throw std::runtime_error(
                "Invalid '--route' value '" + *options.route_spec +
                "'. Expected '<ingress>:<egress>', '<ingress>-><egress>', or '<ingress>,<egress>'"
            );
        }

        config.routing.enabled = true;
        config.routing.ingress_iface = ingress_iface;
        config.routing.egress_iface = egress_iface;
        config.capture.ifaces.clear();
    }

    if (options.bidirectional_route)
        config.routing.bidirectional = true;

    if (config.routing.enabled && !options.ifaces.empty())
        throw std::runtime_error("'-i' cannot be combined with interface routing");

    return config;
}

void SleepWithStopCheck(std::chrono::milliseconds delay, std::chrono::milliseconds poll_interval)
{
    if (poll_interval <= std::chrono::milliseconds::zero())
        poll_interval = std::chrono::milliseconds(1);

    auto remaining = delay;
    while (remaining > std::chrono::milliseconds::zero() && !StopRequested()) {
        const auto sleepFor = remaining > poll_interval ? poll_interval : remaining;
        std::this_thread::sleep_for(sleepFor);
        remaining -= sleepFor;
    }
}

void WaitUntilStopRequested(std::chrono::milliseconds poll_interval)
{
    if (poll_interval <= std::chrono::milliseconds::zero())
        poll_interval = std::chrono::milliseconds(1);

    while (!StopRequested())
        std::this_thread::sleep_for(poll_interval);
}

void LogUnhandledException(const char* context)
{
    try {
        throw;
    }
    catch (const std::exception& e) {
        spdlog::critical("{}: {}", context, e.what());
    }
    catch (...) {
        spdlog::critical("{}: unknown exception", context);
    }
}

void RunCaptureService(const ElephantShrew::RuntimeConfig& config)
{
    ElephantShrew::Bootstrapper bootstrapper;

    spdlog::info("Try to initialize Bootstrapper");
    bootstrapper.Strap();
    bootstrapper.Resolve(config);
    spdlog::info("Capture service is running. Press Ctrl+C to stop.");

    WaitUntilStopRequested(std::chrono::milliseconds(config.supervisor.poll_interval_ms));
}

} // namespace

int main(int argc, char *argv[]) {
    CliOptions options;

    InstallSignalHandlers();

    if (!ParseArguments(argc, argv, options)) {
        PrintUsage(argv[0]);
        return 1;
    }

    ElephantShrew::RuntimeConfig config;
    try {
        config = LoadEffectiveConfig(options);
    }
    catch (...) {
        LogUnhandledException("Failed to load runtime config");
        return 1;
    }

    spdlog::set_level(config.capture.debug_packets ? spdlog::level::debug : spdlog::level::info);

    if (config.ui.show_startup_art)
        PrintStartupArt();

    if (options.scan_only) {
        try {
            return ListInterfaces();
        }
        catch (...) {
            LogUnhandledException("Interface scan failed");
            return 1;
        }
    }

    if (options.config_path)
        spdlog::info("Loaded runtime config from '{}'", *options.config_path);

    if (config.routing.enabled) {
        spdlog::info("Interface routing configured: {} {} {}",
                     config.routing.ingress_iface,
                     config.routing.bidirectional ? "<->" : "->",
                     config.routing.egress_iface);
    }

    spdlog::info("Hello world ElephantShrew");

    while (!StopRequested()) {
        try {
            RunCaptureService(config);
        }
        catch (...) {
            LogUnhandledException("Capture service crashed");
        }

        if (StopRequested())
            break;

        const auto restart_delay = std::chrono::milliseconds(config.supervisor.restart_delay_ms);
        const auto poll_interval = std::chrono::milliseconds(config.supervisor.poll_interval_ms);
        spdlog::warn("Capture service stopped unexpectedly. Restarting in {} ms...", restart_delay.count());
        SleepWithStopCheck(restart_delay, poll_interval);
    }

    spdlog::info("Shutdown requested. Exiting ElephantShrew.");
    return 0;
}
