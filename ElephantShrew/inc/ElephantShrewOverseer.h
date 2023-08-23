/*
 * ElephantShrewOverseer.h
 *
 *  Created on: 3 Jun 2022
 *      Author: Roy Cohen
 */

#ifndef ElephantShrewOVERSEER_H_
#define ElephantShrewOVERSEER_H_

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "IProcessor.h"
#include "ElephantShrewInboundHandler.h"
#include "ElephantShrewOutboundHandler.h"

namespace ElephantShrew {

	class ElephantShrewOverseer {

	public:

		ElephantShrewOverseer(std::shared_ptr<IProcessor> processor,
					   std::shared_ptr<ElephantShrewOutboundHandler> ElephantShrewOutboundHandler,
					   std::shared_ptr<ElephantShrewInboundHandler>  ElephantShrewInboundHandler);
		explicit ElephantShrewOverseer(const ElephantShrewOverseer& other) = delete;
		ElephantShrewOverseer& operator=(const ElephantShrewOverseer& rhs) = delete;
		explicit ElephantShrewOverseer(ElephantShrewOverseer&& other) = delete;
		ElephantShrewOverseer& operator=(const ElephantShrewOverseer&& rhs) = delete;

		void Start();
		void Stop();

		virtual ~ElephantShrewOverseer() = default;

	private:

		void inboundThreadFunction(void);
		void outboundThreadFunction(void);

		std::shared_ptr<IProcessor> 			m_processor;
		std::shared_ptr<ElephantShrewOutboundHandler> 	m_ElephantShrewOutboundHandler;
		std::shared_ptr<ElephantShrewInboundHandler> 	m_ElephantShrewInboundHandler;

		std::queue<uint64_t> 			m_processingOutputs;
		std::atomic<bool> 				m_isInboundStopped;
		std::atomic<bool> 				m_isOutboundStopped;
		mutable std::condition_variable m_condVar;
		mutable std::mutex 				m_lock;

	};

}



#endif /* ElephantShrewOVERSEER_H_ */
