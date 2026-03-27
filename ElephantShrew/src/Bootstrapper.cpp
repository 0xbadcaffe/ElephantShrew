#include <memory>
#include <stdexcept>
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

void Bootstrapper::Resolve(const CaptureOptions& options)
{
    if (!container_)
        throw std::runtime_error("Bootstrapper::Resolve called before Strap");

    auto elephantShrew = container_->resolve<ElephantShrew>();
    elephantShrew->Init(options);
    elephantShrew_ = std::move(elephantShrew);
}

Bootstrapper::~Bootstrapper()
{
    elephantShrew_.reset();
    container_.reset();
    builder_.reset();
}

}
