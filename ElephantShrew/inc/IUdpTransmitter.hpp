#ifndef IUDPTRANSMITTER_H_
#define IUDPTRANSMITTER_H_

#include "ITransmitter.hpp"

namespace ElephantShrew {

class IUdpTransmitter : public ITransmitter {

public:

    virtual ~IUdpTransmitter() = default;
    virtual bool Transmit() = 0;

};
}

#endif // _IUDPTRANSMITTER_H_
