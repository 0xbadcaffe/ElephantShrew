#include "ElephantShrewSuper.hpp"

namespace ElephantShrew {

ElephantShrewSuper::ElephantShrewSuper(
				std::shared_ptr<IProcessor> processor,
				std::shared_ptr<OutboundHandler> outboundHandler,
				std::shared_ptr<InboundHandler>  inboundHandler) :
					   m_processor(processor),
					   m_ElephantShrewOutboundHandler(outboundHandler),
					   m_ElephantShrewInboundHandler(inboundHandler),
					   m_processingOutputs(),
					   m_isInboundStopped(false),
					   m_isOutboundStopped(false),
					   m_condVar(),
					   m_lock() {
}


void ElephantShrewSuper::Start() {

	std::thread inThread(&ElephantShrewSuper::inboundThreadFunction,this);
	std::thread outThread(&ElephantShrewSuper::outboundThreadFunction,this);

	inThread.join();
	outThread.join();

}

void ElephantShrewSuper::Stop() {
	m_isInboundStopped = true;
	m_isOutboundStopped = true;
}

void ElephantShrewSuper::inboundThreadFunction(void) {
	//   while (!m_isInboundStopped) {
	//     std::this_thread::sleep_for(std::chrono::milliseconds(200));
	//     auto newDataLine = m_ElephantShrewInboundHandler->GetInbound();
	//     if(NULL != newDataLine){
	//     	auto newSum = m_processor->Process(newDataLine);
    // 		std::unique_lock<std::mutex> locker(m_lock);
    // 		m_processingOutputs.push(newSum);
	//     }
	//     m_condVar.notify_one();
	//   }
}

void ElephantShrewSuper::outboundThreadFunction(void) {
	//   while (!m_isOutboundStopped) {
	// 	  std::unique_lock<std::mutex> locker(m_lock);
	// 	  m_condVar.wait(locker, [&]{return !m_processingOutputs.empty();});
	// 	  auto sum = m_processingOutputs.front();
	// 	  m_processingOutputs.pop();
	// 	  m_ElephantShrewOutboundHandler->SendOutbound(sum);
	//   }
}

}

