#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <list>
#include <vector>
#include <set>

#include "observer.h"
#include "frame.h"
#include "nic.h"

// -----------------------------------------------------
// Simple Protocol layered over NIC with logical ports.
// - Endpoint = (MAC, port)
// - Header = (srcPort, dstPort, length)
// - Packet = Header + payload (bounded by Ethernet MTU)
// - Async path: NIC notifies this Protocol via Observer<Frame,ProtocolNumber>.
// - Sync path: optional receive() helper (not used when fully async).
// -----------------------------------------------------

template <typename TNIC>
class Protocol : public Observer<NetFrame, NetProtocolType> {
public:
    using NIC      = TNIC;
    using Frame    = NetFrame;
    using ProtocolNumber = NetProtocolType;
    using Address  = NetMacAddress;
    using Port     = uint16_t;

    struct Endpoint {
        Address mac{};
        Port    port{0};
        Endpoint() = default;
        Endpoint(Address a, Port p) : mac(a), port(p) {}
        bool operator==(const Endpoint& o) const { return (mac == o.mac) && (port == o.port); }
    };

    struct Header {
        Port      srcPort{0};
        Port      dstPort{0};
        uint16_t  length{0}; // payload length in bytes
    } __attribute__((packed));

    // MTU available to this protocol (Ethernet payload minus our header)
    static const unsigned MTU = NetFrame::MAX_DATA - sizeof(Header);

    struct Packet : public Header {
        uint8_t data[MTU]; // use only "length" bytes
    } __attribute__((packed));

    // Message type: control or data
    enum class ControlType {
        NONE,   // Not a control message
        READY,
        GO
    };

    // Observers interested in a specific destination port (async API)
    class PortObserver {
    public:
        virtual ~PortObserver() = default;
        virtual Port port() const = 0;
        virtual void on_packet(const Endpoint& from,
                               const Endpoint& to,
                               const uint8_t* data,
                               unsigned len) = 0;

        virtual void on_control(ControlType type,
                                const Endpoint& from,
                                const Endpoint& to) {
            // Default: ignore control messages
            (void)type; (void)from; (void)to;
        }
    };

public:
    Protocol(NIC* n, ProtocolNumber etherType) : nic(n), etherType_(etherType) {
        // Subscribe this Protocol to NICs frame notifications
        nic->attach(this);
    }

    ~Protocol() {nic->detach(this);}

    // Send data payload from "from" to "to" (broadcast MAC is allowed)
    int send(const Endpoint& from, const Endpoint& to, const void* payload, unsigned size) {
        if(size > MTU) return -1;

        Packet pkt{};
        pkt.srcPort = from.port;
        pkt.dstPort = to.port;
        pkt.length = static_cast<uint16_t>(size);
        std::memcpy(pkt.data, payload, size);

        // Serialize: header + payload contiguous
        uint8_t buffer[sizeof(Header) + MTU];
        std::memcpy(buffer, &pkt, sizeof(Header) + size);

        return nic->send(to.mac, etherType_, buffer, static_cast<unsigned>(sizeof(Header) + size));
    }

    // Send control message
    int send_control(const Endpoint& from, const Endpoint& to, ControlType type) {
        std::string msg;
        switch(type) {
            case ControlType::READY: msg = "READY"; break;
            case ControlType::GO:    msg = "GO";    break;
            default: return -1; // Invalid
        }
        return send(from, to, msg.data(), static_cast<unsigned>(msg.size()));
    }

    // Optional sync receive helper (not used in pure async mode)
    int receive(Endpoint& from, Endpoint& to, void* out, unsigned outSize) {
        Address srcMac; ProtocolNumber pnum;
        uint8_t buffer[sizeof(Header) + MTU];

        int n = nic->receive(srcMac, pnum, buffer, sizeof(buffer));
        if(n <= static_cast<int>(sizeof(Header))) return -1; // too small
        if(pnum != etherType_) return -2;

        Header hdr{};
        std::memcpy(&hdr, buffer, sizeof(Header));
        unsigned payloadLen = static_cast<unsigned>(n - static_cast<int>(sizeof(Header)));
        if(payloadLen > outSize) return -3;

        std::memcpy(out, buffer + sizeof(Header), payloadLen);

        from = Endpoint(srcMac, hdr.srcPort);
        to = Endpoint(nic->address(), hdr.dstPort);
        return static_cast<int>(payloadLen);
    }

    // Observer callback from NIC (async path).
    void update(ProtocolNumber pnum, Frame* f) override {
        if(!f) return;
        if(pnum != etherType_) { delete f; return; }
        if(f->size <= sizeof(Header)) { delete f; return; }

        Header hdr{};
        std::memcpy(&hdr, f->data, sizeof(Header));
        unsigned payloadLen = f->size - sizeof(Header);

        AsyncCapsule cap;
        cap.from = Endpoint(f->src, hdr.srcPort);
        cap.to = Endpoint(f->dst, hdr.dstPort);
        cap.length = static_cast<uint16_t>(payloadLen);
        cap.proto = pnum;

        if(payloadLen > 0) {
            cap.payload.resize(payloadLen);
            std::memcpy(cap.payload.data(),
                        f->data + sizeof(Header),
                        payloadLen);
        }

        notify_by_port(cap);
        delete f;
    }

    void enable_sync(int total_nodes, const Endpoint& local) {
        _sync_total_nodes = total_nodes;
        _sync_local = local;
        _sync_ready_nodes.clear();

        // Mark self as ready
        _sync_ready_nodes.insert(local.mac.str());
        send_control(local, Endpoint(Ethernet::Address::BROADCAST(), local.port),
                     ControlType::READY);
    }

    // Allow upper layers to subscribe by destination port
    void attach(PortObserver* po) { portObservers_.push_back(po); }
    void detach(PortObserver* po) { portObservers_.remove(po);   }

//private:
    int _sync_total_nodes{0};
    Endpoint _sync_local{};
    std::set<std::string> _sync_ready_nodes;

    struct AsyncCapsule {
        Endpoint        from;
        Endpoint        to;
        uint16_t        length{0};
        ProtocolNumber  proto{0};
        std::vector<uint8_t> payload;
    };

    void notify_by_port(const AsyncCapsule& c) {
        ControlType ctrl = ControlType::NONE;
        if(c.length == 5 && std::memcmp(c.payload.data(), "READY", 5) == 0) ctrl = ControlType::READY;
        else if(c.length == 2 && std::memcmp(c.payload.data(), "GO", 2) == 0) ctrl = ControlType::GO;

        // Sync logic
        if(ctrl == ControlType::READY && _sync_total_nodes > 0) {
            _sync_ready_nodes.insert(c.from.mac.str());
            if((int)_sync_ready_nodes.size() == _sync_total_nodes)
            {
                // All nodes ready: send GO
                send_control(_sync_local, Endpoint(Ethernet::Address::BROADCAST(), _sync_local.port),
                             ControlType::GO);
            }
        }

        for(auto* po : portObservers_) {
            if(!po) continue;
            if(ctrl != ControlType::NONE) po->on_control(ctrl, c.from, c.to);
            else if(po->port() == c.to.port || po->port() <= -1) po->on_packet(c.from, c.to, c.payload.data(), c.length);
        }
    }

//private:
    NIC* nic;
    ProtocolNumber etherType_;
    std::list<PortObserver*> portObservers_;
};

#endif // PROTOCOL_H
