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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

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
    explicit EngineEthernet(const char* ifname) : sock_(-1), ifindex_(0)
    {
        if(!ifname || !*ifname) throw std::invalid_argument("empty ifname");

        //open raw socket
        sock_ = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if(sock_ < 0) throw std::runtime_error("socket(AF_PACKET) failed");

        //Get network interface index by name, ex: eth0 = 123
        ifindex_ = if_nametoindex(ifname);
        if(ifindex_ == 0) { close_safe(); throw std::runtime_error("if_nametoindex failed"); }

        //Get current MAC address
        {
            struct ifreq ifr{};
            std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
            if(ioctl(sock_, SIOCGIFHWADDR, &ifr) < 0) { close_safe(); throw std::runtime_error("SIOCGIFHWADDR failed"); }
            auto* m = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
            mac_bytes_ = {m[0],m[1],m[2],m[3],m[4],m[5]};
            mac_ = Ethernet::Address(std::array<uint8_t,6>{mac_bytes_[0],mac_bytes_[1],mac_bytes_[2],mac_bytes_[3],mac_bytes_[4],mac_bytes_[5]});
        }

        //Bind to interface
        {
            struct sockaddr_ll sll{};
            sll.sll_family = AF_PACKET;
            sll.sll_protocol = htons(ETH_P_ALL);
            sll.sll_ifindex = static_cast<int>(ifindex_);
            if(bind(sock_, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll)) < 0) 
            {
                close_safe(); 
                throw std::runtime_error("bind(AF_PACKET) failed");
            }
        }

        //Enable async notifications (SIGIO) + non-blocking / without busy-wait
        {
            fcntl(sock_, F_SETOWN, getpid());
            int flags = fcntl(sock_, F_GETFL, 0);
            if(flags < 0) flags = 0;
            fcntl(sock_, F_SETFL, flags | O_ASYNC | O_NONBLOCK);
            std::signal(SIGIO, &EngineEthernet::sigio_handler);
            instance_ = this;
        }
    }

    ~EngineEthernet() override {
        instance_ = nullptr;
        close_safe();
    }

    void bindNIC(NIC* n) { nic = n; }

    // Build ethernet header + payload and send
    int send(const Ethernet::Frame& frame) override {
        if(sock_ < 0) return -1;

        // Ethernet header
        struct ether_header hdr{};
        // dst (parse Address -> bytes; if parsing fails, assume broadcast)
        std::array<uint8_t,6> dst{};
        if(!address_to_bytes(frame.dst, dst)) dst = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        std::memcpy(hdr.ether_dhost, dst.data(), 6);
        std::memcpy(hdr.ether_shost, mac_bytes_.data(), 6);
        hdr.ether_type = htons(frame.proto);

        // Packet buffer: [hdr | payload]
        std::vector<uint8_t> buf(sizeof(hdr) + frame.size);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        std::memcpy(buf.data() + sizeof(hdr), frame.data, frame.size);

        // Destination L2 address
        struct sockaddr_ll sll{};
        sll.sll_family = AF_PACKET;
        sll.sll_protocol = htons(frame.proto);
        sll.sll_ifindex = static_cast<int>(ifindex_);
        sll.sll_halen = 6;
        std::memcpy(sll.sll_addr, dst.data(), 6);

        ssize_t sent = ::sendto(sock_, buf.data(), buf.size(), 0, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll));
        return (sent < 0) ? -1 : static_cast<int>(sent > (ssize_t)sizeof(hdr) ? sent - sizeof(hdr) : 0);
    }

    int start() override {return 0;}

    Ethernet::Address mac() const { return mac_; }

private:
    // Convert address string "AA:BB:CC:DD:EE:FF" -> bytes
    static bool address_to_bytes(const Ethernet::Address& a, std::array<uint8_t,6>& out) {
        auto s = a.str();
        unsigned b0,b1,b2,b3,b4,b5;
        if(std::sscanf(s.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &b0,&b1,&b2,&b3,&b4,&b5) != 6) return false;
        
        out = {
            static_cast<uint8_t>(b0),static_cast<uint8_t>(b1),static_cast<uint8_t>(b2),
            static_cast<uint8_t>(b3),static_cast<uint8_t>(b4),static_cast<uint8_t>(b5)
        };

        return true;
    }

    // SIGIO trampoline
    static void sigio_handler(int) {
        if(instance_) instance_->on_packet();
    }

    // Drain all available frames and forward to NIC
    void on_packet() {
        if(sock_ < 0) return;
        while(true) {
            uint8_t buf[ETH_FRAME_LEN]; // ~1514 bytes
            ssize_t n = ::recvfrom(sock_, buf, sizeof(buf), 0, nullptr, nullptr);
            if(n < 0) break; // EAGAIN when drained

            if(static_cast<size_t>(n) < sizeof(ether_header)) continue;

            ether_header hdr{};
            std::memcpy(&hdr, buf, sizeof(hdr));

            uint16_t proto = ntohs(hdr.ether_type);
            size_t   plen = static_cast<size_t>(n) - sizeof(hdr);
            if(plen > (size_t)Ethernet::Frame::MAX_DATA) plen = Ethernet::Frame::MAX_DATA;

            Ethernet::Frame f;
            
            //convert source host into MAC address byte array
            f.src = Ethernet::Address(std::array<uint8_t,6>{
                hdr.ether_shost[0],hdr.ether_shost[1],hdr.ether_shost[2],
                hdr.ether_shost[3],hdr.ether_shost[4],hdr.ether_shost[5]
            });
            
            //convert destination host into MAC address byte array
            f.dst = Ethernet::Address(std::array<uint8_t,6>{
                hdr.ether_dhost[0],hdr.ether_dhost[1],hdr.ether_dhost[2],
                hdr.ether_dhost[3],hdr.ether_dhost[4],hdr.ether_dhost[5]
            });

            f.proto = proto; //protocol
            f.size = static_cast<unsigned>(plen);
            std::memcpy(f.data, buf + sizeof(hdr), plen);

            if(nic) nic->on_frame(f);
        }
    }

    void close_safe() { if(sock_ >= 0) { ::close(sock_); sock_ = -1; } }

private:
    int                      sock_;
    unsigned                 ifindex_;
    std::array<uint8_t,6>    mac_bytes_{};
    Ethernet::Address        mac_;
    static EngineEthernet*   instance_;
};

// static
EngineEthernet* EngineEthernet::instance_ = nullptr;

#endif // ENGINE_ETHERNET_H
