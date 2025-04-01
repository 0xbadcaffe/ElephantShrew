#include <memory>
#include <boost/version.hpp>
#include <spdlog/spdlog.h>
//#include <tbb/flow_graph.h>
#include "Bootstrapper.hpp"
#include "ElephantShrew.hpp"
#include "NetworkInterfaceScanner.hpp"

namespace ElephantShrew
{

void Bootstrapper::Strap()
{    
    std::string boost_version = BOOST_LIB_VERSION;
    spdlog::info("Boost version: {}", boost_version);

    builder_ = std::make_shared< Hypodermic::ContainerBuilder >();

    builder_->registerType<ElephantShrew>()
        .as<IElephantShrew>(); 

    builder_->registerType<NetworkInterfaceScanner>();

    container_ = builder_->build();
}

void Bootstrapper::Resolve()
{
    auto elephantShrew = container_->resolve< ElephantShrew >();
    auto networkIntScanner = container_->resolve< NetworkInterfaceScanner >();

    elephantShrew->Init();
    //networkIntScanner->Scan();
}

Bootstrapper::~Bootstrapper()
{
    //TODO clear IOC container
}

}