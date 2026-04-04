#ifndef _ITRANSMITTER_H_
#define _ITRANSMITTER_H_

namespace ElephantShrew {

class ITransmitter {

public:

        virtual ~ITransmitter() = default;
        virtual void Transmit() = 0;

    };
}

#endif // _ITRANSMITTER_H_
