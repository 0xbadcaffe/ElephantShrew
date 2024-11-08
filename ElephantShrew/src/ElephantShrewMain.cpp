#include <iostream>
#include "Bootstrapper.hpp"



int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[]) {

    std::cout << "Hello ElephantShrew" << std::endl;

    //ElephantShrew::Bootstrapper bootstrapper;

    try
    {
        std::cout << " try initializing ElephantShrew" << std::endl;
        /* Create ElephantShrew IOC container */
        //auto ElephantShrewOverseer = ElephantShrew.Init();
        /* Start processing packets */
        //ElephantShrewOverseer->Start();
    }
    catch (const std::exception& e)
    {
        std::cout << " exception from ElephantShrew: '" << e.what() << "'\n";
    }

	return 0;

}

