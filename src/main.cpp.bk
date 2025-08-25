#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static void print_mac(const unsigned char* m) 
{
    std::ios old(nullptr); old.copyfmt(std::cout);
    std::cout << std::hex << std::uppercase << std::setfill('0')
              << std::setw(2) << (int)m[0] << ":" << std::setw(2) << (int)m[1] << ":"
              << std::setw(2) << (int)m[2] << ":" << std::setw(2) << (int)m[3] << ":"
              << std::setw(2) << (int)m[4] << ":" << std::setw(2) << (int)m[5];
    std::cout.copyfmt(old);
}

static bool get_iface_mac(int ctl_fd, const char* ifname, unsigned char mac[6]) 
{
    ifreq ifr{}; std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    if (ioctl(ctl_fd, SIOCGIFHWADDR, &ifr) < 0) return false;
    std::memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return true;
}

static bool iface_is_up(int ctl_fd, const char* ifname) 
{
    ifreq ifr{}; std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    if (ioctl(ctl_fd, SIOCGIFFLAGS, &ifr) < 0) return false;
    return (ifr.ifr_flags & IFF_UP);
}

//send ethernet frame as ascii with broadcast
static ssize_t send_broadcast_msg(int fd, const char* ifname, uint16_t ether_type, const std::string& msg) 
{
    unsigned char src[6]{}; if (!get_iface_mac(fd, ifname, src)) { errno = ENODEV; return -1; }
    unsigned char dst[6]; std::memset(dst, 0xFF, 6);

    struct EthHdr { unsigned char dst[6], src[6]; uint16_t type_be; } __attribute__((packed));
    std::vector<unsigned char> frame(sizeof(EthHdr) + msg.size());
    auto* h = reinterpret_cast<EthHdr*>(frame.data());
    std::memcpy(h->dst, dst, 6);
    std::memcpy(h->src, src, 6);
    h->type_be = htons(ether_type);
    std::memcpy(frame.data() + sizeof(EthHdr), msg.data(), msg.size());
    return ::send(fd, frame.data(), frame.size(), 0);
}

int main(int argc, char** argv) 
{
    const char* ifname = (argc > 1 ? argv[1] : "eth0");
    const uint16_t MY_ETHER_TYPE = 0x88B5; //whatever u want :)

    //block sigrtmin and sigint signals on main thread
    int signo = SIGRTMIN;
    sigset_t set; sigemptyset(&set); sigaddset(&set, signo); sigaddset(&set, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {std::cerr << "pthread_sigmask error\n"; return 1;}

    //raw socket with interface bind
    int s = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (s < 0) { perror("socket"); return 1; }

    unsigned ifindex = if_nametoindex(ifname);
    if (!ifindex) { perror("if_nametoindex"); return 1; }

    sockaddr_ll sll{}; sll.sll_family = AF_PACKET; sll.sll_protocol = htons(ETH_P_ALL); sll.sll_ifindex = ifindex;
    if (::bind(s, (sockaddr*)&sll, sizeof(sll)) < 0) { perror("bind"); return 1; }

    if (!iface_is_up(s, ifname)) std::cerr << "Interface " << ifname << " DOWN, to solve: ip link set " << ifname << " up\n";

    // sigout async delivery
    unsigned char my_mac[6]{}; 
    int flags = fcntl(s, F_GETFL, 0);

    if (fcntl(s, F_SETOWN, getpid()) < 0) { perror("F_SETOWN"); return 1; }
    if (fcntl(s, F_SETSIG, signo)< 0) { perror("F_SETSIG"); return 1; }
    if (fcntl(s, F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) { perror("F_SETFL"); return 1; }
    if (!get_iface_mac(s, ifname, my_mac)) { std::cerr << "Falha ao obter MAC\n"; return 1; }

    //tx thread, send periodic broadcast
    std::atomic<bool> keep{true};
    std::thread tx([&] {
        unsigned i = 0;
        while (keep.load(std::memory_order_relaxed)) {
            std::string msg = "oi mundo #" + std::to_string(i++);
            ssize_t n = send_broadcast_msg(s, ifname, MY_ETHER_TYPE, msg);
            if (n < 0) 
            {
                int e = errno;
                if (e == ENETDOWN || e == ENETUNREACH || e == EHOSTDOWN) std::cerr << "[TX] rede down/unreach — suba a interface (ip link set " << ifname << " up)\n";    
                else std::cerr << "[TX] send: " << std::strerror(e) << "\n";
            } 
            else std::cout << "[TX] broadcast (" << n << " bytes): " << msg << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        }
    });

    std::cout << "Aguardando eventos (SIGRTMIN = RX; Ctrl+C = sair)...\n";

    //blocking loop without busy-wait, using kernel signals
    for (;;) {
        siginfo_t si{};
        int r = sigwaitinfo(&set, &si);  //block until receive sigwaitinfo exception
        if (r < 0) { perror("sigwaitinfo"); break; }
        if (r == SIGINT) {                // Ctrl+C para encerrar com graça
            std::cout << "\n[SIGINT] Encerrando...\n";
            break;
        }

        //arrived rx event, print ready frames with self mac filter
        unsigned char buf[2048];
        ulong received_time = 0; //time arrived
        for (;;) {
            ssize_t n = ::recv(s, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // nothig pendent
                perror("recv"); break;
            }
            if (n < 14) { std::cout << "[RX] frame curto (" << n << " bytes)\n"; continue; }

            const unsigned char* dst = buf + 0;
            const unsigned char* src = buf + 6;
            uint16_t type_be; std::memcpy(&type_be, buf + 12, 2);
            uint16_t etype = ntohs(type_be);

            // filter (src == my_mac)
            if (std::memcmp(src, my_mac, 6) == 0) continue;

            std::cout << "[RX] " << n << "B dst="; print_mac(dst);
            std::cout << " src="; print_mac(src);
            std::ios old(nullptr); old.copyfmt(std::cout);
            std::cout << " type=0x" << std::hex << std::setw(4) << std::setfill('0') << etype;
            std::cout.copyfmt(old);

            if (n > 14) {
                std::string payload(reinterpret_cast<char*>(buf + 14), reinterpret_cast<char*>(buf + n));
                std::cout << " payload: \"" << payload.substr(0, 64) << (payload.size() > 64 ? "..." : "") << "\"";
            }
            std::cout << "\n";
        }
    }

    // END
    keep = false;
    tx.join();
    close(s);
    return 0;
}
