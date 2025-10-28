#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <list>
#include <vector>
#include <set>
#include <sstream>
#include <functional>

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

        static Endpoint endpoint_from_string(const std::string& s) {
            auto pos = s.find_last_of(':');
            if (pos == std::string::npos) return {};
            std::string mac_str = s.substr(0, pos);
            uint16_t port = static_cast<uint16_t>(std::stoi(s.substr(pos + 1)));
            return Endpoint(Address::from_string(mac_str), port);
        }
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
        GO,
        SYNC
    };

    struct Message {
        uint8_t  orig_vm;      // Original sender VM (from first sender)
        uint16_t orig_port;    // Original sender port
        uint64_t timestamp;    // Original TIME
        int      type;         // Message type (like component port)
        uint64_t msg_id;       // Original ID
        std::string body;      // Payload body
        ControlType control{ControlType::NONE}; // Control type, default NONE
    };

    static std::string build_message(const Message& msg) {
        std::ostringstream oss;
        oss << "VM=" << int(msg.orig_vm) << " "
            << "PORT=" << msg.orig_port << " "
            << "TIME=" << msg.timestamp << " "
            << "TYPE=" << msg.type << " "
            << "ID=" << msg.msg_id << " "
            << msg.body;
        return oss.str();
    }
   
    // Observers interested in a specific destination port (async API)
    class PortObserver {
    public:
        ChannelOrigin origin;  // store the observer’s channel
        PortObserver(ChannelOrigin o) : origin(o) {}
        virtual ~PortObserver() = default;
        virtual Port port() const = 0;
        virtual void on_packet(const Endpoint& from,
                               const Endpoint& to,
                               const uint8_t* data,
                               unsigned len,
                               ChannelOrigin origin_of_packet) = 0;

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

    static Message parse_message(const std::vector<uint8_t>& payload) {
        Message msg;
        std::string s(payload.begin(), payload.end());
        size_t pos = 0;

        auto extract_field = [&](const std::string& key) -> std::string {
            size_t p = s.find(key + "=", pos);
            if (p == std::string::npos) return {};
            p += key.size() + 1;
            size_t end = s.find(' ', p);
            if (end == std::string::npos) end = s.size();
            pos = end + 1;
            return s.substr(p, end - p);
        };

        if (auto v = extract_field("VM"); !v.empty()) msg.orig_vm = static_cast<uint8_t>(std::stoi(v));
        if (auto v = extract_field("PORT"); !v.empty()) msg.orig_port = static_cast<uint16_t>(std::stoi(v));
        if (auto v = extract_field("TIME"); !v.empty()) msg.timestamp = std::stoull(v);
        if (auto v = extract_field("TYPE"); !v.empty()) msg.type = std::stoi(v);
        if (auto v = extract_field("ID"); !v.empty()) msg.msg_id = std::stoull(v);

        if (pos < s.size()) {
            msg.body = s.substr(pos, s.size() - pos); // safe even if pos == s.size()
        } else {
            msg.body.clear(); // explicitly empty
        }    

        return msg;
    }

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
/*     int receive(Endpoint& from, Endpoint& to, void* out, unsigned outSize) {
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
    } */

    // Observer callback from NIC (async path).
    void update(ProtocolNumber pnum, Frame* f, ChannelOrigin origin) override {
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
        cap.origin = origin;

        if(payloadLen > 0) {
            cap.payload.resize(payloadLen);
            std::memcpy(cap.payload.data(),
                        f->data + sizeof(Header),
                        payloadLen);
        }

        notify_by_port(cap);
        delete f;
    }

    // Enable sync with a callback
    void enable_sync(int total_vms, const Endpoint& local, std::function<void()> cb = {}) {
        _sync_total_vms = total_vms;
        _sync_local = local;
        _sync_ready_nodes.clear();
        _sync_done_callback = cb;

        // Mark self as ready
        _sync_ready_nodes.insert(_sync_local.mac.str());
        printf("[SYNC] %s Marking itself ready, total ready: %zu/%d\n", _sync_local.mac.str().c_str(), _sync_ready_nodes.size(), _sync_total_vms);
    }

    // Allow upper layers to subscribe by destination port
    void attach(PortObserver* po) { portObservers_.push_back(po); }
    void detach(PortObserver* po) { portObservers_.remove(po);   }

    struct AsyncCapsule {
        Endpoint        from;
        Endpoint        to;
        uint16_t        length{0};
        ProtocolNumber  proto{0};
        ChannelOrigin   origin;
        std::vector<uint8_t> payload;
    };

    void notify_by_port(const AsyncCapsule& c) {
        Message msg = parse_message(c.payload);
        //printf("[DEBUG] Protocol layer received message [%s] from %s\n", msg.body.c_str(), c.from.mac.str().c_str());

        // Clean up nulls and whitespace
        msg.body.erase(std::find(msg.body.begin(), msg.body.end(), '\0'), msg.body.end());
        msg.body.erase(std::remove_if(msg.body.begin(), msg.body.end(), ::isspace), msg.body.end());

        // Determine control type
        ControlType ctrl = ControlType::NONE;
        if(msg.body == "READY") ctrl = ControlType::READY;
        else if(msg.body == "GO") ctrl = ControlType::GO;

        // Sync logic for READY
        if(ctrl == ControlType::READY && _sync_total_vms > 0) {
            // Track which nodes are ready
            _sync_ready_nodes.insert(c.from.mac.str());
            printf("[SYNC] READY received from %s, total ready: %zu/%d\n", c.from.mac.str().c_str(), _sync_ready_nodes.size(), _sync_total_vms);

            // If all nodes are ready, send GO (only if not already sent)
            if(!_sync_go_sent && (int)_sync_ready_nodes.size() == _sync_total_vms) {
                printf("[SYNC] All nodes ready, sending GO broadcast\n");
                send_control(_sync_local, Endpoint(Ethernet::Address::BROADCAST(), _sync_local.port), ControlType::GO);
                _sync_go_sent = true;   // mark that we already sent GO
            }
        }

        // If GO received, notify owner
        if(ctrl == ControlType::GO) {
            if(!_sync_go_received) {
                printf("[SYNC] GO received from %s\n", c.from.mac.str().c_str());
                _sync_go_received = true;   // prevent reacting multiple times
                if(_sync_done_callback) _sync_done_callback();
            }
        }

        // Forward to port observers
        for(auto* po : portObservers_) {
            if(!po) continue;
            if(ctrl != ControlType::NONE) {
                po->on_control(ctrl, c.from, c.to);
            } else if(po->port() == c.to.port || po->port() <= -1) {
                po->on_packet(c.from, c.to, c.payload.data(), c.length, c.origin);
            }
        }
    }

private:
    int _sync_total_vms{0};
    Endpoint _sync_local{};
    std::set<std::string> _sync_ready_nodes;
    NIC* nic;
    ProtocolNumber etherType_;
    std::list<PortObserver*> portObservers_;
    std::function<void()> _sync_done_callback;
    bool _sync_go_sent = false;
    bool _sync_go_received = false;
};

#endif // PROTOCOL_H
