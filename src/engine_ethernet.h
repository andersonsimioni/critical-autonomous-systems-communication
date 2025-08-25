#ifndef ENGINE_ETHERNET_H
#define ENGINE_ETHERNET_H

#include "engine.h"
#include "nic.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <cstring>

// -----------------------------------------------------
// Engine using raw socket + SIGIO for async receive (simplified)
// -----------------------------------------------------
class EngineEthernet : public Engine {
public:
    explicit EngineEthernet(const char* ifname)
    : sock(-1)
    {
        (void)ifname; // TODO: bind to interface here (real impl)
        // Open raw socket for all EtherTypes (demo)
        sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if(sock < 0) { perror("socket"); throw std::runtime_error("socket failed"); }

        // Async notifications via SIGIO
        fcntl(sock, F_SETOWN, getpid());
        fcntl(sock, F_SETFL, O_ASYNC);

        // Demo MAC (locally administered 02:..) — troque por ioctl(SIOCGIFHWADDR) na versão real
        std::array<uint8_t,6> demo{{0x02,0x00,0xDE,0xAD,0xBE,0xEF}};
        mac_ = Ethernet::Address(demo);

        std::signal(SIGIO, EngineEthernet::sigio_handler);
        instance = this;
    }

    ~EngineEthernet() override {
        if(sock >= 0) close(sock);
    }

    int send(const Ethernet::Frame& frame) override {
        // DEMO: só loga. Na versão real: montar ethhdr e sendto(sock, ...)
        std::cout << "[Ethernet] sending frame from " << frame.src.str()
                  << " to " << frame.dst.str()
                  << " proto=0x" << std::hex << frame.proto << std::dec
                  << " size=" << frame.size << "\n";
        return static_cast<int>(frame.size);
    }

    int start() override {
        std::cout << "[Ethernet] async receive enabled\n";
        return 0;
    }

    Ethernet::Address mac() const { return mac_; }

    // SIGIO trampoline
    static void sigio_handler(int) {
        if(instance) instance->on_packet();
    }

    void on_packet() {
        // DEMO: simula um frame recebido; use recvfrom() na versão real
        Ethernet::Frame f;
        std::array<uint8_t,6> srcBytes{{0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}};
        f.src = Ethernet::Address(srcBytes);
        f.dst = Ethernet::Address::BROADCAST();
        f.proto = 0x88B5;
        f.size = 2;
        f.data[0] = 7; f.data[1] = 9;

        std::cout << "[Ethernet] SIGIO -> new frame received\n";
        if(nic) nic->on_frame(f);
    }

private:
    int sock;
    Ethernet::Address mac_;
    static EngineEthernet* instance;
};

EngineEthernet* EngineEthernet::instance = nullptr;

#endif // ENGINE_ETHERNET_H
