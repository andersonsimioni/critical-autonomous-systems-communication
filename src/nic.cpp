#include "nic.h"
#include "communicator.h"

NIC::NIC(Engine* ethernet_engine, Engine* shm_engine, Address addr) : ethernet_engine(ethernet_engine), shm_engine(shm_engine), mac(addr) {
    if(ethernet_engine != NULL) ethernet_engine->bindNIC(this);
    shm_engine->bindNIC(this);
}

int NIC::send(Address dst, Protocol proto, const void* data, unsigned int size) {
    Frame f;
    f.src = mac;
    f.dst = dst;
    f.proto = proto;
    f.size = size;
    std::memcpy(f.data, data, size);

    const bool is_shm = (dst == this->address());
    if (is_shm) {
        if (!this->shm_engine) { printf("SHM engine missing!\n"); return -1; }
        //printf("NIC sending through SHM\n");
        return this->shm_engine->send(f);
    } else {
        if (!this->ethernet_engine) {
            // Defensive: print clear error — components shouldn't attempt direct Ethernet
            fprintf(stderr, "NIC::send(): no ethernet engine available for dst=%s; intended for gateway forwarding\n", dst.str().c_str());
            return -2;
        }
        //printf("NIC sending through ETHERNET\n");
        return this->ethernet_engine->send(f);
    }
}

// called by Ethernet Engine when a frame is ready
void NIC::on_eth_frame(const Frame& f) {
    // Ignore frames sent by myself
    if(f.src == mac) return;

    // Copy frame and notify all observers registered on this NIC
    Frame* copy = new Frame(f);
    this->notify(f.proto, copy, ChannelOrigin::Ethernet);
    //printf("A frame is arriving through ETHERNET\n");
}


// called by SHM Engine when a frame is ready
void NIC::on_shm_frame(const Frame& f) {
    // Ignore frames sent by myself
    // if(f.src == mac) return;

    // Copy frame and notify all observers registered on this NIC
    Frame* copy = new Frame(f);
    this->notify(f.proto, copy, ChannelOrigin::SharedMemory);
}

const NIC::Address& NIC::address() const {
    return mac;
}