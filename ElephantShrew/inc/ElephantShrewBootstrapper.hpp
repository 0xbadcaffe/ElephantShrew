#ifndef _ELEPHANTSHREWBOOTSTRAPPER_
#define _ELEPHANTSHREWBOOTSTRAPPER_


namespace ElephantShrew
{

    class ElephantShrewBootstrapper
    {

    public:

        ElephantShrewBootstrapper() = default;
        explicit ElephantShrewBootstrapper(const ElephantShrewBootstrapper& other) = delete;
        ElephantShrewBootstrapper& operator=(const ElephantShrewBootstrapper& rhs) = delete;
        explicit ElephantShrewBootstrapper(ElephantShrewBootstrapper&& other) = delete;
        ElephantShrewBootstrapper& operator=(const ElephantShrewBootstrapper&& rhs) = delete;

        void Strap();

        virtual ~ElephantShrewBootstrapper() = default;

    };

}

#endif //_ELEPHANTSHREWBOOTSTRAPPER_