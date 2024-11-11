#ifndef _ELEPHANTSHREWSUPER_H_
#define _ELEPHANTSHREWSUPER_H_

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "IProcessor.hpp"
#include "InboundHandler.hpp"
#include "OutboundHandler.hpp"

namespace ElephantShrew {

    class ElephantShrewSuper {

    public:

        ElephantShrewSuper(std::shared_ptr<IProcessor> processor,
        std::shared_ptr<OutboundHandler> OutboundHandler,
        std::shared_ptr<InboundHandler>  InboundHandler);
        explicit ElephantShrewSuper(const ElephantShrewSuper& other) = delete;
        ElephantShrewSuper& operator=(const ElephantShrewSuper& rhs) = delete;
        explicit ElephantShrewSuper(ElephantShrewSuper&& other) = delete;
        ElephantShrewSuper& operator=(const ElephantShrewSuper&& rhs) = delete;

        void Start();
        void Stop();

        virtual ~ElephantShrewSuper() = default;

    private:

        void inboundThreadFunction(void);
        void outboundThreadFunction(void);

        std::shared_ptr<IProcessor>         m_processor;
        std::shared_ptr<OutboundHandler>    m_ElephantShrewOutboundHandler;
        std::shared_ptr<InboundHandler>     m_ElephantShrewInboundHandler;

        std::queue<uint64_t>            m_processingOutputs;
        std::atomic<bool>               m_isInboundStopped;
        std::atomic<bool>               m_isOutboundStopped;
        mutable std::condition_variable m_condVar;
        mutable std::mutex              m_lock;

    };

}

#endif // _ELEPHANTSHREWsuper_H_
