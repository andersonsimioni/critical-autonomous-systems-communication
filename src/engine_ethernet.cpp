#include "engine_ethernet.h"

#include <array>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <csignal>
#include <thread>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

// static
EngineEthernet* EngineEthernet::instance_ = nullptr;
std::thread rx_thread_;
static sem_t packet_sem;

EngineEthernet::EngineEthernet(const char* ifname) : sock_(-1), ifindex_(0), running(true)
{
    if(!ifname || !*ifname) throw std::invalid_argument("empty ifname");

    //Open raw socket
    sock_ = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if(sock_ < 0) throw std::runtime_error("socket(AF_PACKET) failed");

    // Get network interface index by name, ex: eth0 = 123
    ifindex_ = if_nametoindex(ifname);
    if(ifindex_ == 0) { close_safe(); throw std::runtime_error("if_nametoindex failed"); }

    // Get current MAC address
    {
        struct ifreq ifr{};
        std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
        if(ioctl(sock_, SIOCGIFHWADDR, &ifr) < 0) { close_safe(); throw std::runtime_error("SIOCGIFHWADDR failed"); }
        auto* m = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
        mac_bytes_ = {m[0],m[1],m[2],m[3],m[4],m[5]};
        mac_ = Ethernet::Address(std::array<uint8_t,6>{mac_bytes_[0],mac_bytes_[1],mac_bytes_[2],mac_bytes_[3],mac_bytes_[4],mac_bytes_[5]});
    }

    // Bind to interface
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

    // Enable async notifications (SIGIO) + non-blocking / without busy-wait
    {
        // Initialize semaphore with 0 tokens for incoming packet notification via signal
        sem_init(&packet_sem, 0, 0);

        // Set up SIGIO handler
        struct sigaction sa{};
        sa.sa_handler = EngineEthernet::sigio_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGIO, &sa, nullptr);

        fcntl(sock_, F_SETOWN, getpid());
        int flags = fcntl(sock_, F_GETFL, 0);
        if(flags < 0) flags = 0;
        fcntl(sock_, F_SETFL, flags | O_ASYNC | O_NONBLOCK);
        instance_ = this;
    }
}

EngineEthernet::~EngineEthernet()
{
    sem_destroy(&packet_sem);
    running = false;
    instance_ = nullptr;
    close_safe();
}

// Bind NIC
void EngineEthernet::bindNIC(NIC* n) { nic = n; }

// Build ethernet header + payload and send
int EngineEthernet::send(const Ethernet::Frame& frame) 
{
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

// Receive loop
void EngineEthernet::rx_loop() {
    while (running) {
        // This blocks until the semaphore is posted by SIGIO
        sem_wait(&packet_sem);

        if (!running) break;

        on_packet(); // Drain all packets in the socket buffer
    }
}

// Start
int EngineEthernet::start() {
    rx_thread_ = std::thread([this] { rx_loop(); });
    return 0;
}

// Get own MAC address
Ethernet::Address EngineEthernet::mac() const { return mac_; }

// SIGIO
void EngineEthernet::sigio_handler(int) {
    //Async-signal-safe: just post the semaphore
    sem_post(&packet_sem);
}

// Drain all available frames and forward to NIC
void EngineEthernet::on_packet()
{
    if(sock_ < 0) return;

    while(true)
    {
        uint8_t buf[ETH_FRAME_LEN]; // ~1514 bytes
        ssize_t n = ::recvfrom(sock_, buf, sizeof(buf), 0, nullptr, nullptr);
        if(n < 0) break; //EAGAIN when drained

        if(static_cast<size_t>(n) < sizeof(ether_header)) continue;

        ether_header hdr{};
        std::memcpy(&hdr, buf, sizeof(hdr));

        uint16_t proto = ntohs(hdr.ether_type);
        size_t plen = static_cast<size_t>(n) - sizeof(hdr);
        if(plen > Ethernet::Frame::MAX_DATA) plen = Ethernet::Frame::MAX_DATA;

        Ethernet::Frame f;
        f.src = Ethernet::Address({hdr.ether_shost[0],hdr.ether_shost[1],hdr.ether_shost[2], hdr.ether_shost[3],hdr.ether_shost[4],hdr.ether_shost[5]});
        f.dst = Ethernet::Address({hdr.ether_dhost[0],hdr.ether_dhost[1],hdr.ether_dhost[2], hdr.ether_dhost[3],hdr.ether_dhost[4],hdr.ether_dhost[5]});
        f.proto = proto;
        f.size = static_cast<unsigned>(plen);
        std::memcpy(f.data, buf + sizeof(hdr), plen);

        if(nic) nic->on_eth_frame(f);
    }
}

// Close socket safely
void EngineEthernet::close_safe()
{
    if(sock_ >= 0) { ::close(sock_); sock_ = -1; }
}

// Convert address string "AA:BB:CC:DD:EE:FF" -> bytes
bool EngineEthernet::address_to_bytes(const Ethernet::Address& a, std::array<uint8_t,6>& out)
{
    auto s = a.str();
    unsigned b0,b1,b2,b3,b4,b5;
        if(std::sscanf(s.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &b0,&b1,&b2,&b3,&b4,&b5) != 6) return false;

        out = {
            static_cast<uint8_t>(b0),static_cast<uint8_t>(b1),static_cast<uint8_t>(b2),
            static_cast<uint8_t>(b3),static_cast<uint8_t>(b4),static_cast<uint8_t>(b5)
        };
        
    return true;
}

