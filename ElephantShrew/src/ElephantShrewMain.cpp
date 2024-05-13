/*
 * main.cpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */
#include "ElephantShrew.h"


int main(int argc, char *argv[]) {

	std::cout << "Hello ElephantShrew" << std::endl;

	ElephantShrew::ElephantShrew ElephantShrew;

    try
    {
    	/* Create ElephantShrew IOC container */
    	auto ElephantShrewOverseer = ElephantShrew.Init();
    	/* Start processing packets */
    	ElephantShrewOverseer->Start();
    }
    catch (const std::exception& e)
    {
        std::cout << " exception from ElephantShrew: '" << e.what() << "'\n";
    }

	return 0;

}

