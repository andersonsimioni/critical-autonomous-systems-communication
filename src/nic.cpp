#include "nic.h"

NIC::NIC(Engine* ethernet_engine, Engine* shm_engine, Address addr) : ethernet_engine(ethernet_engine), shm_engine(shm_engine), mac(addr) {
    ethernet_engine->bindNIC(this);
    shm_engine->bindNIC(this);
}

int NIC::send(Address dst, Protocol proto, const void* data, unsigned int size) {
    Frame f;
    f.src = mac;
    f.dst = dst;
    f.proto = proto;
    f.size = size;
    std::memcpy(f.data, data, size);

    return dst.str() == this->address().str() ? shm_engine->send(f) : ethernet_engine->send(f);
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