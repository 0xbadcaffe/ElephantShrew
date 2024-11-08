#ifndef IElephantShrewTRANSMITTER_H_
#define IElephantShrewTRANSMITTER_H_

namespace ElephantShrew {

class ITransmitter {

public:

        virtual ~ITransmitter() = default;
        virtual bool Transmit() = 0;

    };
}

#endif // _ITRANSMITTER_H_
