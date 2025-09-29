#ifndef ENGINE_ETHERNET_H
#define ENGINE_ETHERNET_H

#include "engine.h"
#include "ethernet.h"
#include "nic.h"

#include <array>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <csignal>

// -----------------------------------------------------
// Minimal raw Ethernet engine (AF_PACKET + SIGIO)
// - Grabs REAL MAC via ioctl(SIOCGIFHWADDR)
// - Binds to <ifname>
// - Async receive with SIGIO (O_ASYNC + F_SETOWN)
// - send(): build ether header + payload and sendto()
// - on_packet(): recvfrom(), parse header, forward to NIC
// -----------------------------------------------------
class EngineEthernet : public Engine {
public:
    explicit EngineEthernet(const char* ifname);
    ~EngineEthernet() override;

    void bindNIC(NIC* n);
    int send(const Ethernet::Frame& frame) override; 
    int start() override;
    Ethernet::Address mac() const;

private:
    void rx_loop();
    void on_packet();
    void close_safe();
    static bool address_to_bytes(const Ethernet::Address& a, std::array<uint8_t,6>& out);
    static void sigio_handler(int);

private:
    int                      sock_;
    unsigned                 ifindex_;
    std::array<uint8_t,6>    mac_bytes_{};
    Ethernet::Address        mac_;
    static EngineEthernet*   instance_;
    bool                     running{true};
};

#endif // ENGINE_ETHERNET_H
