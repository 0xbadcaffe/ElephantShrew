#include <memory>
#include <tbb/flow_graph.h>
#include "Bootstrapper.hpp"
#include "ElephantShrew.hpp"

namespace ElephantShrew
{

void Bootstrapper::Strap()
{
    _builder = std::make_shared<Hypodermic::ContainerBuilder>();

    _builder->registerType<ElephantShrew>()
        .as<IElephantShrew>(); 

    _container = builder->build();
}

void Bootstrapper::Resolve()
{
    auto elephantShrew = _container->resolve< ElephantShrew >();

    elephantShrew.E
}

Bootstrapper::~Bootstrapper()
{

}

}