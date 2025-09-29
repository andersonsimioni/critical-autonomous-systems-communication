#include "nic.h"

NIC::NIC(Engine* ethernet_engine, Engine* shm_engine, Address addr) : ethernet_engine(ethernet_engine), shm_engine(shm_engine), mac(addr) {
    ethernet_engine->bindNIC(this);
    shm_engine->bindNIC(this);
}

int NIC::send(Address dst, Protocol proto, const void* data, unsigned int size) {
    if(this->shm_engine == nullptr) printf("SHM Engine not found!\n");
    // if(this->ethernet_engine == nullptr) printf("ETHERNET Engine not found!\n");

    Frame f;
    f.src = mac;
    f.dst = dst;
    f.proto = proto;
    f.size = size;
    std::memcpy(f.data, data, size);

    const bool is_shm = (dst == this->address());
    printf(is_shm ? "NIC sending through SHM\n" : "NIC sending through ETHERNET\n");
    return is_shm ? this->shm_engine->send(f) : this->ethernet_engine->send(f);
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