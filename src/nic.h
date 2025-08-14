#ifndef NIC_H
#define NIC_H

#include <string>
#include <cstdint>
#include "ethernet.h"
#include "engine.h"

class NIC {
public:
    using Address = Ethernet::Address;

    // iface example: tap0, eth0..
    NIC(Engine& engine, const std::string& iface);
    ~NIC() = default;

    // NIC of MAC
    const Address& address() const { return _mac; }

    // Set MAC manually
    void address(const Address& mac) { _mac = mac; }

    // Send a payload with EtherType passed, dst is broadcast
    bool send(const Address& dst, uint16_t etherType, const void* data, size_t size);

    // Receive the byte len of a payload, also the received etherType and MAC
    int receive(Address* src, uint16_t* etherType, void* out, size_t maxSize);

private:
    bool fetchInterfaceMAC(const std::string& iface);

private:
    Engine& _engine;
    Address _mac;
    std::string _iface;
};

#endif // NIC_H
