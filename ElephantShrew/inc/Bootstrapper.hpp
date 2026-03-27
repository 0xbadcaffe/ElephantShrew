#ifndef _BOOTSTRAPPER_
#define _BOOTSTRAPPER_

#include <Hypodermic/Hypodermic.h>
#include <string>
#include <vector>

namespace ElephantShrew
{

    class Bootstrapper
    {

    public:

        Bootstrapper() = default;
        explicit Bootstrapper(const Bootstrapper& other) = delete;
        Bootstrapper& operator=(const Bootstrapper& rhs) = delete;
        explicit Bootstrapper(Bootstrapper&& other) = delete;
        Bootstrapper& operator=(const Bootstrapper&& rhs) = delete;

        void Strap();
        void Resolve(const std::vector<std::string>& ifaces = {});

        virtual ~Bootstrapper();

    private:
        std::shared_ptr<Hypodermic::ContainerBuilder> builder_;
        std::shared_ptr<Hypodermic::Container>        container_;

    };

}

#endif // _BOOTSTRAPPER_