#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "Bootstrapper.hpp"

namespace {

void AppendInterfaces(std::vector<std::string>& ifaces, const std::string& value)
{
    std::stringstream stream(value);
    std::string iface;

    while (std::getline(stream, iface, ',')) {
        if (!iface.empty())
            ifaces.push_back(iface);
    }
}

bool ParseInterfaces(int argc, char* argv[], std::vector<std::string>& ifaces)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-i" || arg == "-s") {
            if (i + 1 >= argc) {
                spdlog::error("Missing value for '{}'", arg);
                return false;
            }

            AppendInterfaces(ifaces, argv[++i]);
            continue;
        }

        spdlog::error("Unknown argument '{}'", arg);
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char *argv[]) {

    std::vector<std::string> ifaces;

    if (!ParseInterfaces(argc, argv, ifaces)) {
        std::cout << "Usage: " << argv[0] << " [-i <iface>]... [-s <iface1,iface2,...>]\n";
        return 1;
    }

    spdlog::info("Hello world ElephantShrew");

    ElephantShrew::Bootstrapper bootstrapper;

    try
    {
        spdlog::info("Try to initialize Bootstrapper");
        /* Create ElephantShrew IOC container */
        bootstrapper.Strap();
        bootstrapper.Resolve(ifaces);
        /* Start processing packets */
        //ElephantShrewOverseer->Start();
    }
    catch (const std::exception& e)
    {
        std::cout << " exception from ElephantShrew: '" << e.what() << "'\n";
    }

	return 0;

}
