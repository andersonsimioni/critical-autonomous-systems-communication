#ifndef NIC_H
#define NIC_H

#include <algorithm>
#include <cstring>
#include "ethernet.h"
#include "frame.h"
#include "engine.h"
#include "observer.h"

// Forward decl to avoid circular problems if needed
class NIC;

// -----------------------------------------------------
// NIC as an Observed subject of frames
// -----------------------------------------------------
class NIC : public Ethernet,
            public Observed<NetFrame, NetProtocolType>
{
public:
    NIC(Engine* e, Address addr) : engine(e), mac(addr) {
        engine->bindNIC(this);
    }

    int send(Address dst, Protocol proto, const void* data, unsigned int size) {
        Frame f;
        f.src = mac;
        f.dst = dst;
        f.proto = proto;
        f.size = size;
        std::memcpy(f.data, data, size);
        return engine->send(f);
    }

    // called by Engine when a frame is ready
    void on_frame(const Frame& f) {
        // copy, then notify all observers registered on this NIC
        if(f.src == mac) return;
        Frame* copy = new Frame(f);
        this->notify(f.proto, copy);
    }

    const Address& address() const { return mac; }

private:
    Engine* engine;
    Address mac;
};

#endif // NIC_H
