#ifndef ETHERNET_H
#define ETHERNET_H

#include <cstdint>
#include <string>

namespace Ethernet {

    static const int ADDR_LEN = 6; // MAC SIZE
    static const int MTU = 1500; // PAYLOAD SIZE
    static const int FRAME_MAX = 1514;

    // MAC ADDRESS
    struct Address {
        uint8_t bytes[ADDR_LEN];

        Address();
        Address(const uint8_t* addr);
        static Address broadcast();

        std::string toString() const;
        bool operator==(const Address& other) const;
        bool operator!=(const Address& other) const;
    } __attribute__((packed));

    // Header of Ethernet
    struct Header {
        Address dst;
        Address src;
        uint16_t type; // EtherType
    } __attribute__((packed));

    // Ethernet Frame
    struct Frame {
        Header header;
        uint8_t payload[MTU];
    } __attribute__((packed));

} // namespace Ethernet

#endif // ETHERNET_H
