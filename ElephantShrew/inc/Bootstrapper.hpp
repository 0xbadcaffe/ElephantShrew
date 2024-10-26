#ifndef _BOOTSTRAPPER_
#define _BOOTSTRAPPER_

#include <Hypodermic/Hypodermic.h>

namespace ElephantShrew
{

    class Bootstrapper
    {

    public:

        Bootstrapper() = default;
        explicit Bootstrapper(const Bootstrapper& other) = delete;
        Bootstrapper& operator=(const Bootstrapper& rhs) = delete;
        explicit ElephantShrewBootstrapper(Bootstrapper&& other) = delete;
        Bootstrapper& operator=(const Bootstrapper&& rhs) = delete;

        void Strap();
        void Resolve();

        virtual ~Bootstrapper();

    private:
        std::shared_ptr<ContainerBuilder> _builder;
        std::shared_ptr<Container>        _container;

    };

}

#endif // _BOOTSTRAPPER_