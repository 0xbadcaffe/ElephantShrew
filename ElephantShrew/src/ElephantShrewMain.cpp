#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
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
    ElephantShrew::CaptureOptions capture;
    bool scan_only{false};
};

void PrintUsage(const char* program)
{
    std::cout << "Usage: " << program << " [-s] [-r] [-d] [-i <iface>]...\n";
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

        if (arg == "-i") {
            if (i + 1 >= argc) {
                spdlog::error("Missing value for '{}'", arg);
                return false;
            }

            options.capture.ifaces.push_back(argv[++i]);
            continue;
        }

        if (arg == "-s") {
            options.scan_only = true;
            continue;
        }

        if (arg == "-r") {
            options.capture.record_packets = true;
            continue;
        }

        if (arg == "-d") {
            options.capture.debug_packets = true;
            continue;
        }

        spdlog::error("Unknown argument '{}'", arg);
        return false;
    }

    if (options.scan_only &&
        (!options.capture.ifaces.empty() || options.capture.record_packets || options.capture.debug_packets)) {
        spdlog::error("'-s' cannot be combined with capture flags");
        return false;
    }

    return true;
}

void SleepWithStopCheck(std::chrono::milliseconds delay)
{
    constexpr auto kSleepSlice = std::chrono::milliseconds(200);

    auto remaining = delay;
    while (remaining > std::chrono::milliseconds::zero() && !StopRequested()) {
        const auto sleepFor = remaining > kSleepSlice ? kSleepSlice : remaining;
        std::this_thread::sleep_for(sleepFor);
        remaining -= sleepFor;
    }
}

void WaitUntilStopRequested()
{
    while (!StopRequested())
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
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

void RunCaptureService(const CliOptions& options)
{
    ElephantShrew::Bootstrapper bootstrapper;

    spdlog::info("Try to initialize Bootstrapper");
    bootstrapper.Strap();
    bootstrapper.Resolve(options.capture);
    spdlog::info("Capture service is running. Press Ctrl+C to stop.");

    WaitUntilStopRequested();
}

} // namespace

int main(int argc, char *argv[]) {
    constexpr auto kRestartDelay = std::chrono::seconds(2);

    CliOptions options;

    InstallSignalHandlers();

    if (!ParseArguments(argc, argv, options)) {
        PrintUsage(argv[0]);
        return 1;
    }

    if (options.scan_only) {
        try {
            return ListInterfaces();
        }
        catch (...) {
            LogUnhandledException("Interface scan failed");
            return 1;
        }
    }

    spdlog::set_level(options.capture.debug_packets ? spdlog::level::debug : spdlog::level::info);

    spdlog::info("Hello world ElephantShrew");

    while (!StopRequested()) {
        try {
            RunCaptureService(options);
        }
        catch (...) {
            LogUnhandledException("Capture service crashed");
        }

        if (StopRequested())
            break;

        spdlog::warn("Capture service stopped unexpectedly. Restarting in {} seconds...", kRestartDelay.count());
        SleepWithStopCheck(std::chrono::duration_cast<std::chrono::milliseconds>(kRestartDelay));
    }

    spdlog::info("Shutdown requested. Exiting ElephantShrew.");
    return 0;
}
