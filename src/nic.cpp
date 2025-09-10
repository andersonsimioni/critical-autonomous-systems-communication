#include "nic.h"

NIC::NIC(Engine* ethernet_engine, Engine* shm_engine, Address* addr) : ethernet_engine(ethernet_engine), shm_engine(shm_engine) {
    ethernet_engine->bindNIC(this);
    shm_engine->bindNIC(this);

    memcpy(&this->mac, addr, sizeof(Address));
    printf("..\n");
    printf("NIC MAC ON CONSTRUCTOR = %d %d %d %d %d %d\n", this->mac.addr[0], this->mac.addr[1], this->mac.addr[2], this->mac.addr[3], this->mac.addr[4], this->mac.addr[5]);
}

int NIC::send(Address dst, Protocol proto, const void* data, unsigned int size) {

    /* const bool is_shm = (dst == this->address());

    const std::string dst_s  = dst.str();
    const std::string mac_s  = this->address().str();
    const std::string mac_eng  = ((EngineEthernet*)(this->ethernet_engine))->mac().str();

    printf("dst = %s  this_address = %s  ethernet_engine = %s\n", dst_s.c_str(), mac_s.c_str(), mac_eng.c_str());
    printf(is_shm ? "NIC sending through SHM\n" : "NIC sending through ETHERNET\n");
 */
    printf("%d %d %d %d %d %d\n", dst.addr[0], dst.addr[1], dst.addr[2], dst.addr[3], dst.addr[4], dst.addr[5]);
    printf("%d %d %d %d %d %d\n", this->mac.addr[0], this->mac.addr[1], this->mac.addr[2], this->mac.addr[3], this->mac.addr[4], this->mac.addr[5]);

    Frame f;
    f.src = mac;
    f.dst = dst;
    f.proto = proto;
    f.size = size;
    std::memcpy(f.data, data, size);

    bool is_shm = true;
    return is_shm ? shm_engine->send(f) : ethernet_engine->send(f);
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