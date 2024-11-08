#include <iostream>
#include "Bootstrapper.hpp"
#include <spdlog/spdlog.h>



int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[]) {

    spdlog::info("Hello world ElephantShrew");

    ElephantShrew::Bootstrapper bootstrapper;

    try
    {
        spdlog::info("Try to initialize Bootstrapper");
        /* Create ElephantShrew IOC container */
        bootstrapper.Strap();
        bootstrapper.Resolve();
        /* Start processing packets */
        //ElephantShrewOverseer->Start();
    }
    catch (const std::exception& e)
    {
        std::cout << " exception from ElephantShrew: '" << e.what() << "'\n";
    }

	return 0;

}

