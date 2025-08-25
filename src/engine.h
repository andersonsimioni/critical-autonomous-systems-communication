#ifndef ENGINE_H
#define ENGINE_H

#include "ethernet.h"

class NIC; // forward declaration

// -----------------------------------------------------
// Abstract Engine
// -----------------------------------------------------
class Engine {
public:
    virtual ~Engine() {}
    void bindNIC(NIC* n) { nic = n; }

    virtual int send(const Ethernet::Frame& frame) = 0;
    virtual int start() = 0; // begin async receive

protected:
    NIC* nic = nullptr; // back reference to NIC
};

#endif // ENGINE_H
