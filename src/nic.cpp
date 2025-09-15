#include "nic.h"

NIC::NIC(Engine* e, Address addr) : engine(e), mac(addr) {
    engine->bindNIC(this);
}

int NIC::send(Address dst, Protocol proto, const void* data, unsigned int size) {
    Frame f;
    f.src = mac;
    f.dst = dst;
    f.proto = proto;
    f.size = size;
    std::memcpy(f.data, data, size);
    return engine->send(f);
}

// called by Engine when a frame is ready
void NIC::on_frame(const Frame& f) {
    // Ignore frames sent by myself
    // if(f.src == mac) return;

    // Copy frame and notify all observers registered on this NIC
    Frame* copy = new Frame(f);
    this->notify(f.proto, copy);
}

const NIC::Address& NIC::address() const {
    return mac;
}