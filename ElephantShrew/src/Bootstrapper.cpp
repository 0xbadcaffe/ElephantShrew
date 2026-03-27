#include <memory>
#include <boost/version.hpp>
#include <spdlog/spdlog.h>
#include "Bootstrapper.hpp"
#include "ElephantShrew.hpp"

namespace ElephantShrew
{

void Bootstrapper::Strap()
{    
    std::string boost_version = BOOST_LIB_VERSION;
    spdlog::info("Boost version: {}", boost_version);

    builder_ = std::make_shared< Hypodermic::ContainerBuilder >();

    builder_->registerType<ElephantShrew>()
        .as<IElephantShrew>();

    container_ = builder_->build();
}

void Bootstrapper::Resolve(const std::vector<std::string>& ifaces)
{
    auto elephantShrew = container_->resolve< ElephantShrew >();
    elephantShrew->Init(ifaces);
}

Bootstrapper::~Bootstrapper()
{
    //TODO clear IOC container
}

}