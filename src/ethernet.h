#ifndef ETHERNET_H
#define ETHERNET_H

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

// -----------------------------------------------------
// Ethernet definitions
// -----------------------------------------------------
class Ethernet {
public:
    static constexpr std::size_t MTU = 1500; // Standard Ethernet payload
    
    // Ethernet MAC address (6 bytes)
    class Address {
    public:
        static const int LENGTH = 6;
        std::array<uint8_t,LENGTH> addr;
        static Address BROADCAST() { return Address({0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}); }

        Address() { addr.fill(0); }
        Address(const std::array<uint8_t,LENGTH>& a) : addr(a) {}

        bool operator==(const Address& other) const { return addr == other.addr; }
        bool operator!=(const Address& other) const { return !(*this==other); }

        std::string str() const {
            char buf[18];
            std::snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X", addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
            return std::string(buf);
        }
    };

    // Ethernet protocol type (16 bits)
    using Protocol = uint16_t;

    // Ethernet frame
    struct Frame {
        Address dst;
        Address src;
        Protocol proto;
        static const int MAX_DATA = 1500;
        uint8_t data[MAX_DATA];
        unsigned int size; // real size of data

        Frame() : proto(0), size(0) {}
    };

    //  Função para imprimir um frame completo
    static void print_frame(const Frame& f) {
        printf("===== FRAME =====\n");
        printf("SRC: %s\n", f.src.str().c_str());
        printf("DST: %s\n", f.dst.str().c_str());
        printf("PROTO: 0x%04X\n", static_cast<unsigned>(f.proto));
        printf("SIZE: %u bytes\n", f.size);

        printf("PAYLOAD (texto): ");
        for (unsigned i = 0; i < f.size; i++) {
            char c = static_cast<char>(f.data[i]);
            if (std::isprint(static_cast<unsigned char>(c)))
                putchar(c);
            else
                putchar('.');
        }
        printf("\n=================\n");
    }
};

#endif // ETHERNET_H