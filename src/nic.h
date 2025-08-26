#ifndef NIC_H
#define NIC_H

#include <algorithm>
#include <cstring>
#include "ethernet.h"
#include "frame.h"
#include "engine.h"
#include "observer.h"

// Forward declaration to avoid circular dependencies
class NIC;

// -----------------------------------------------------
// NIC as an Observed subject of frames
// -----------------------------------------------------
class NIC : public Ethernet,
            public Observed<NetFrame, NetProtocolType>
{
public:
    NIC(Engine* e, Address addr);

    int send(Address dst, Protocol proto, const void* data, unsigned int size);

    void on_frame(const Frame& f);

    const Address& address() const;

private:
    Engine* engine;
    Address mac;
};

#endif // NIC_H
