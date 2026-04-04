#ifndef _IRECEIVER_H_
#define _IRECEIVER_H_

#include <memory>

namespace ElephantShrew {

class IReceiver {

public:

    virtual ~IReceiver() = default;
    virtual void Receive() = 0;

};

}

#endif // _I_RECEIVER_H_
