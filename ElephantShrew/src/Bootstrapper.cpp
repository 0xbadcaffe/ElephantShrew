#include <memory>
//#include <tbb/flow_graph.h>
#include "Bootstrapper.hpp"
#include "ElephantShrew.hpp"
#include "NetworkInterfaceScanner.hpp"

namespace ElephantShrew
{

void Bootstrapper::Strap()
{
    builder_ = std::make_shared< Hypodermic::ContainerBuilder >();

    builder_->registerType<ElephantShrew>()
        .as<IElephantShrew>(); 

    // builder_->registerType<NetworkInterfaceScanner>()
    //     .as<NetworkInterfaceScanner>(); 

    container_ = builder_->build();
}

void Bootstrapper::Resolve()
{
    auto elephantShrew = container_->resolve< ElephantShrew >();

    elephantShrew->Init();
}

Bootstrapper::~Bootstrapper()
{
    //TODO clear IOC container
}

}