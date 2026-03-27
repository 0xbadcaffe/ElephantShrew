#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <optional>
#include <pcapplusplus/PcapLiveDeviceList.h>
#include <spdlog/spdlog.h>
#include <iostream>
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
    bool record_packets{false};
    bool debug_packets{false};
    bool scan_only{false};
};

void PrintUsage(const char* program)
{
    std::cout << "Usage: " << program << " [-s] [-r] [-d] [-c <config.json>] [-i <iface>]...\n";
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
        (!options.ifaces.empty() || options.record_packets || options.debug_packets)) {
        spdlog::error("'-s' cannot be combined with capture flags");
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
