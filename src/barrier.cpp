#include <iostream>
#include <cstring>
#include <set>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>

constexpr int NUM_VMS = 5;
constexpr const char* IFACE = "eth0";  // interface dentro da VM
constexpr uint16_t ETH_PROTO = 0x88B5; // protocolo Ethernet customizado

struct Msg {
    char type[6]; // "READY" ou "START"
    int id;
};

// Função para obter o MAC local
bool get_mac_address(const char* ifname, uint8_t mac[6]) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    ifreq ifr{};
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        return false;
    }
    close(fd);

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return true;
}

// Função para enviar mensagem Ethernet
void send_msg(int sockfd, const Msg& msg, const uint8_t src_mac[6], const uint8_t dest_mac[6]) {
    uint8_t buffer[1500];
    ether_header* eh = (ether_header*)buffer;

    memcpy(eh->ether_dhost, dest_mac, 6);
    memcpy(eh->ether_shost, src_mac, 6);
    eh->ether_type = htons(ETH_PROTO);

    memcpy(buffer + sizeof(ether_header), &msg, sizeof(msg));

    sockaddr_ll addr{};
    addr.sll_ifindex = if_nametoindex(IFACE);
    addr.sll_halen = 6;
    memcpy(addr.sll_addr, dest_mac, 6);

    sendto(sockfd, buffer, sizeof(ether_header) + sizeof(msg), 0,
           (sockaddr*)&addr, sizeof(addr));
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <ID_VM>\n";
        return 1;
    }
    int id = std::stoi(argv[1]);

    int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_PROTO));
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Obtem MAC local
    uint8_t local_mac[6];
    if (!get_mac_address(IFACE, local_mac)) {
        std::cerr << "Erro ao obter MAC de " << IFACE << "\n";
        return 1;
    }

    std::cout << "[VM " << id << "] MAC local: "
              << std::hex << (int)local_mac[0] << ":"
              << (int)local_mac[1] << ":"
              << (int)local_mac[2] << ":"
              << (int)local_mac[3] << ":"
              << (int)local_mac[4] << ":"
              << (int)local_mac[5] << std::dec << "\n";

    // Endereço destino: broadcast
    uint8_t dest_mac[6]; memset(dest_mac, 0xff, 6);

    // Envia READY
    Msg m{};
    strncpy(m.type, "READY", sizeof(m.type));
    m.id = id;
    send_msg(sockfd, m, local_mac, dest_mac);
    std::cout << "[VM " << id << "] Enviou READY\n";

    // Espera todos os READY
    std::set<int> ready_ids; ready_ids.insert(id);
    uint8_t buffer[1600];
    while (ready_ids.size() < NUM_VMS) {
        int n = recv(sockfd, buffer, sizeof(buffer), 0);
        if (n > (int)sizeof(ether_header)) {
            Msg* rec = (Msg*)(buffer + sizeof(ether_header));
            if (strncmp(rec->type, "READY", 5) == 0) {
                ready_ids.insert(rec->id);
                std::cout << "[VM " << id << "] Recebeu READY de " << rec->id << "\n";
            }
        }
    }

    // Envia START
    strncpy(m.type, "START", sizeof(m.type));
    send_msg(sockfd, m, local_mac, dest_mac);
    std::cout << "[VM " << id << "] Enviou START\n";

    // Espera todos os START
    std::set<int> start_ids; start_ids.insert(id);
    while (start_ids.size() < NUM_VMS) {
        int n = recv(sockfd, buffer, sizeof(buffer), 0);
        if (n > (int)sizeof(ether_header)) {
            Msg* rec = (Msg*)(buffer + sizeof(ether_header));
            if (strncmp(rec->type, "START", 5) == 0) {
                start_ids.insert(rec->id);
                std::cout << "[VM " << id << "] Recebeu START de " << rec->id << "\n";
            }
        }
    }

    std::cout << "[VM " << id << "] Todos sincronizados, iniciando aplicação crítica...\n";
    // system("./app_critica");

    close(sockfd);
    return 0;
}
