/*
 * ElephantShrewOverseer.cpp
 *
 *  Created on: 3 Jun 2022
 *      Author: Roy Cohen
 */
#include "ElephantShrewOverseer.h"

namespace ElephantShrew {

ElephantShrewOverseer::ElephantShrewOverseer(
				std::shared_ptr<IProcessor> processor,
				std::shared_ptr<ElephantShrewOutboundHandler> ElephantShrewOutboundHandler,
				std::shared_ptr<ElephantShrewInboundHandler>  ElephantShrewInboundHandler) :
					   m_processor(processor),
					   m_ElephantShrewOutboundHandler(ElephantShrewOutboundHandler),
					   m_ElephantShrewInboundHandler(ElephantShrewInboundHandler),
					   m_processingOutputs(),
					   m_isInboundStopped(false),
					   m_isOutboundStopped(false),
					   m_condVar(),
					   m_lock() {
}


void ElephantShrewOverseer::Start() {

	std::thread inThread(&ElephantShrewOverseer::inboundThreadFunction,this);
	std::thread outThread(&ElephantShrewOverseer::outboundThreadFunction,this);

	inThread.join();
	outThread.join();

}

void ElephantShrewOverseer::Stop() {
	m_isInboundStopped = true;
	m_isOutboundStopped = true;
}

void ElephantShrewOverseer::inboundThreadFunction(void) {
	  while (!m_isInboundStopped) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(200));
	    auto newDataLine = m_ElephantShrewInboundHandler->GetInbound();
	    if(NULL != newDataLine){
	    	auto newSum = m_processor->Process(newDataLine);
    		std::unique_lock<std::mutex> locker(m_lock);
    		m_processingOutputs.push(newSum);
	    }
	    m_condVar.notify_one();
	  }
}

void ElephantShrewOverseer::outboundThreadFunction(void) {
	  while (!m_isOutboundStopped) {
		  std::unique_lock<std::mutex> locker(m_lock);
		  m_condVar.wait(locker, [&]{return !m_processingOutputs.empty();});
		  auto sum = m_processingOutputs.front();
		  m_processingOutputs.pop();
		  m_ElephantShrewOutboundHandler->SendOutbound(sum);
	  }
}

}

