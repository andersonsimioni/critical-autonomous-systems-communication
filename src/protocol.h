#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <list>
#include <vector>
#include <set>
#include <sstream>
#include <functional>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <iostream>

#include "observer.h"
#include "frame.h"
#include "nic.h"
#include "utils.h"
#include "clock_syncer.h"


static const uint8_t SECRET_KEY[] = { 0x41, 0x23, 0x55, 0x88 }; // chave compartilhada
static const size_t SECRET_KEY_LEN = sizeof(SECRET_KEY);

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
    using SyncCallback = std::function<void(uint64_t master_time)>;

    // Wildcard port for observers that want to receive all packets
    static constexpr Port ANY_PORT = 0xFFFF;
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
        SYNC_REQ,
        SYNC_RESP,
        DELAY_REQ,
        DELAY_RESP,
        GROUP_MOVE,
        GROUP_MOVE_NOTIFY,
        GROUP_ASSIGN
    };

    struct Message {
        uint8_t  group_id;     // Road segment of the original sender VM (from first sender)
        uint8_t  orig_vm;      // Original sender VM
        uint16_t orig_port;    // Original sender port
        uint64_t timestamp;    // Original TIME
        int      type;         // Message type (like component port)
        uint64_t msg_id;       // Original ID
        uint8_t msgac;             // Message auth code
        std::string body;      // Payload body
        ControlType control{ControlType::NONE}; // Control type, default NONE
    };

    static std::string build_message(const Message& msg) {
        std::ostringstream oss;
        oss << "GROUP=" << int(msg.group_id) << " "
            << "VM=" << int(msg.orig_vm) << " "
            << "PORT=" << msg.orig_port << " "
            << "TIME=" << msg.timestamp << " "
            << "TYPE=" << msg.type << " "
            << "ID=" << msg.msg_id << " "
            << "CONTROL=" << static_cast<int>(msg.control) << " "
            << "MSGAC=" << int(msg.msgac) << " "
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
        _clock_syncer = new ClockSyncer();

        _running_ptp = false;
        _probabilistic_ptp_timeout = false;
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

        if (auto v = extract_field("GROUP"); !v.empty()) msg.group_id = static_cast<uint8_t>(std::stoi(v));
        if (auto v = extract_field("VM"); !v.empty()) msg.orig_vm = static_cast<uint8_t>(std::stoi(v));
        if (auto v = extract_field("PORT"); !v.empty()) msg.orig_port = static_cast<uint16_t>(std::stoi(v));
        if (auto v = extract_field("TIME"); !v.empty()) msg.timestamp = std::stoull(v);
        if (auto v = extract_field("TYPE"); !v.empty()) msg.type = std::stoi(v);
        if (auto v = extract_field("ID"); !v.empty()) msg.msg_id = std::stoull(v);
        if (auto v = extract_field("CONTROL"); !v.empty()) msg.control = static_cast<ControlType>(std::stoi(v));
        if (auto v = extract_field("MSGAC"); !v.empty()) msg.msgac = std::stoull(v);

        if (pos < s.size()) {
            msg.body = s.substr(pos, s.size() - pos); // safe even if pos == s.size()
        } else {
            msg.body.clear(); // explicitly empty
        }    

        return msg;
    }

    // Helper to get the group of a VM from its mac address
    std::optional<int> get_group_of(int vm_id) const {
        std::lock_guard<std::mutex> lk(groups_mtx);
        for (auto& [gid, info] : _groups) {
            if (info.members.count(vm_id)) return gid;
        }
        return std::nullopt;
    }

    // Helper to extract VM id from a MAC address (assumes last octet encodes vm id)
    static inline int vm_id_from_mac(const Address &mac) {
        return static_cast<int>(mac.addr[5]); // assume Address has a public array 'addr' of 6 bytes like uint8_t addr[6];
    }

    // Helper to build a MAC address string "00:00:00:00:00:XX" for given vm_id, then make Endpoint
    Endpoint endpoint_from_vm(int vm_id, Port port) const {
        if (vm_id < 0 || vm_id > 255) throw std::runtime_error("vm_id out of range");
        char buf[32]; // vm_id must fit in a byte (0..255), format as hex two-digit
        snprintf(buf, sizeof(buf), "00:00:00:00:00:%02x", vm_id & 0xFF);
        return Endpoint(Address::from_string(std::string(buf)), port);
    }

    uint8_t generate_mac(const char* buf) {
        uint8_t mac = 0;
        size_t len = strlen(buf);

        uint8_t msgac = 0;
            for (size_t i = 0; i < len; i++) {
                msgac ^= buf[i] ^ SECRET_KEY[i % SECRET_KEY_LEN];
            }
            return msgac;
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
    int send_control(const Endpoint& from, const Endpoint& to, ControlType ctype, uint8_t msgac = 0, const std::string& payload = {}) {
        Message msg;
        msg.control = ctype;                                            // explicit control type
        msg.msgac = msgac;                                              // message auth control
        msg.group_id = static_cast<uint8_t>(current_group_id);          // metadata so receivers can reason about group/origin
        msg.orig_vm = static_cast<uint8_t>(vm_id_from_mac(from.mac));   // extract vm_id from MAC address
        msg.orig_port = from.port;
        msg.timestamp = get_microseconds_now();
        msg.body = payload;                                             // extra data

        // Serialize message
        std::string serialized = build_message(msg);

        return send(from, to, serialized.data(), static_cast<unsigned>(serialized.size()));
    }

    int get_current_group_id() const { return current_group_id; }

    void set_current_group(int gid) {
        {
            std::lock_guard<std::mutex> lk(groups_mtx);
            current_group_id = gid;

            // Register local VM in the group's member list so lookups succeed.
            try {
                int my_vm = vm_id_from_mac(nic->address());
                _groups[gid].group_id = gid;             // ensure group struct exists
                _groups[gid].members.insert(my_vm);      // insert local VM id
            } catch(...) {
                // shouldn't happen, but be safe
            }
        }
        printf("[DEBUG] Group id set to %d (registered local vm)\n", current_group_id);
    }

    std::set<int> group_members(int gid) const {
        std::lock_guard<std::mutex> lk(groups_mtx);
        if (auto it = _groups.find(gid); it != _groups.end())
            return it->second.members;
        return {};
    }

    void broadcast_group_info(int gid) {
        std::ostringstream oss;
        oss << "GROUP_INFO " << gid << " MEMBERS:";
        for (auto &m : _groups[gid].members) oss << " " << m;
        std::string msg = oss.str();
        send(_control_local, Endpoint(Ethernet::Address::BROADCAST(), _control_local.port), msg.data(), msg.size());
    }

    void move_group(int new_gid) {send_control(_control_local, Endpoint(Ethernet::Address::BROADCAST(), _control_local.port),
        ControlType::GROUP_MOVE, std::to_string(new_gid));}

    // Enable sync barrier with a callback
    void enable_start_sync(int total_vms, const Endpoint& local, std::function<void()> cb = {}) {
        _sync_total_vms = total_vms;
        _control_local = local;
        _sync_ready_nodes.clear();
        _sync_done_callback = cb;

        if (!_is_master) {
            // Only non-master nodes mark themselves ready
            _sync_ready_nodes.insert(_control_local.mac.str());
            //printf("[SYNC] %s Marking itself ready, total ready: %zu/%d\n",
            //    _control_local.mac.str().c_str(), _sync_ready_nodes.size(), _sync_total_vms);
        }
    }

    // Periodic clock synchronization
    bool _is_master{false};
    void set_master(bool m) { _is_master = m; }

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
            std::memcpy(cap.payload.data(), f->data + sizeof(Header), payloadLen);
        }

        notify_by_port(cap);
        delete f;
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

        // Remove trailing nulls only (if any)
        if (!msg.body.empty()) {
            auto nullpos = msg.body.find('\0');
            if (nullpos != std::string::npos) msg.body.resize(nullpos);
        }

        // Trim leading/trailing whitespace but preserve internal whitespace
        auto ltrim = [](std::string &s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
        };
        auto rtrim = [](std::string &s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
        };
        ltrim(msg.body); rtrim(msg.body);

        // Determine control type
        ControlType ctrl = msg.control;

        // Sync logic for READY
        if(ctrl == ControlType::READY && _sync_total_vms > 0) {
            // Track which nodes are ready
            _sync_ready_nodes.insert(c.from.mac.str());
            printf("[SYNC] READY received from %s, total ready: %zu/%d\n", c.from.mac.str().c_str(), _sync_ready_nodes.size(), _sync_total_vms);

            // If all nodes are ready, send GO (only if not already sent)
            if(!_sync_go_sent && (int)_sync_ready_nodes.size() == _sync_total_vms) {
                printf("[SYNC] All nodes ready, sending GO broadcast\n");
                send_control(_control_local, Endpoint(Ethernet::Address::BROADCAST(), _control_local.port), ControlType::GO);
                _sync_go_sent = true;   // mark that we already sent GO
            }
        }

        // If GO received, notify owner
        else if(ctrl == ControlType::GO) {
            if(!_sync_go_received) {
                printf("[SYNC] GO received from %s\n", c.from.mac.str().c_str());
                _sync_go_received = true;   // prevent reacting multiple times
                if(_sync_done_callback) _sync_done_callback();
            }
        }

        // SYNC CLOCKS PROTOCOL:

        else if(ctrl == ControlType::SYNC_REQ) {
            if(_is_master) {
                // Master received SYNC_REQ from worker
                uint64_t t1 = get_microseconds_now();  // master current time

                char buf[128];
                snprintf(buf, sizeof(buf), "%lu", (unsigned long)t1);

                // PROVISORIO
                uint8_t msgac = generate_mac(buf);

                // Send SYNC_RESP with timestamp t1
                send_control(_control_local, c.from, ControlType::SYNC_RESP, msgac, buf);

                //printf("[SYNC] SYNC_REQ received from %s, sending SYNC_RESP with t1=%llu\n", c.from.mac.str().c_str(), (unsigned long long)t1);
            }
        }

        else if(ctrl == ControlType::SYNC_RESP && !_is_master) {
            // Worker received SYNC from master
            uint64_t t1 = 0;
            if(!msg.body.empty()) sscanf(msg.body.c_str(), "%lu", (unsigned long*)&t1);

            uint64_t t2 = get_microseconds_now(); // worker receive time
            uint64_t t3 = t2; // worker send time, the same for now

            char buf[256];
            snprintf(buf, sizeof(buf), "%lu %lu %lu", (unsigned long)t1, (unsigned long)t2, (unsigned long)t3);

            // PROVISORIO
            uint8_t msgac = generate_mac(buf);
            
            send_control(_control_local, c.from, ControlType::DELAY_REQ, msgac, buf);

            //printf("[SYNC] Received SYNC_RESP from %s (t1=%llu), sending DELAY_REQ (t2=%llu, t3=%llu)\n", c.from.mac.str().c_str(), (unsigned long long)t1, (unsigned long long)t2, (unsigned long long)t3);
        }

        else if(ctrl == ControlType::DELAY_REQ && _is_master) {
            uint64_t t1, t2, t3;
            if(sscanf(msg.body.c_str(), "%lu %lu %lu", (unsigned long*)&t1, (unsigned long*)&t2, (unsigned long*)&t3) != 3) return;

            uint64_t t4 = get_microseconds_now(); // master receive time

            // Compute NTP-style offset and delay
            int64_t offset = ((int64_t)(t2 - t1) + (int64_t)(t3 - t4)) / 2;
            int64_t rtt = (t4 - t1) - (t3 - t2);

            // Send DELAY_RESP with offset & rtt
            char buf[128];
            snprintf(buf, sizeof(buf), "%ld %ld", (long)offset, (long)rtt);

            // PROVISORIO
            uint8_t msgac = generate_mac(buf);

            send_control(_control_local, c.from, ControlType::DELAY_RESP, msgac, buf);

            //printf("[SYNC] DELAY_REQ from %s (t1=%llu t2=%llu t3=%llu t4=%llu)\n offset=%lld microseconds rtt=%lld microseconds\n", c.from.mac.str().c_str(), (unsigned long long)t1, (unsigned long long)t2, (unsigned long long)t3, (unsigned long long)t4, (long long)offset, (long long)rtt);
        }

        else if(ctrl == ControlType::DELAY_RESP && !_is_master) {
            int64_t offset = 0;
            int64_t delay = 0;
            
            sscanf(msg.body.c_str(), "%lld %lld", (long long*)&offset, (long long*)&delay);
            printf("Clock Syncer Collecting Sample offset=%ld delay=%ld, adding samples\n", offset, delay);

            _clock_syncer->addPtpSample(offset, delay);
            printf("samples added!\n");

            auto sync_over_time = this->get_probabilistic_ptp_timeout();
            auto enough_samples = _clock_syncer->hasEnoughSamplesCI(100, 1.96); //+-50us with 95% confidence
            printf("calculate overtime or enough samples with success!\n");

            if(true) 
            {
                printf("[SYNC] applying sync..\n");
                if(_clock_syncer->applySync()) printf("[SYNC] sync applyed with success!\n");
                set_running_ptp(false);
                
                //std::exit(EXIT_FAILURE);
            }
            else
            {
                printf("Protocol Asking other PTP step..\n");
                send_control(_control_local, c.from, Protocol<TNIC>::ControlType::SYNC_REQ);
                printf("Protocol Asked PTP step with success!\n");
            }

            //printf("[SYNC] DELAY_RESP received from master %s (offset=%lld)\n", c.from.mac.str().c_str(), (long long)offset);
        }

        else if (ctrl == ControlType::GROUP_MOVE) {
            int requester_id = static_cast<int>(msg.orig_vm); // VM that wants to move
            int requested_gid = -1;
            try {
                requested_gid = std::stoi(msg.body); // desired target group
            } catch(...) {
                printf("[GROUP] Bad GROUP_MOVE body from %d: '%s'\n", requester_id, msg.body.c_str());
                return;
            }

            auto sender_gid = get_group_of(requester_id);
            if (!sender_gid) {
                printf("[GROUP] MOVE: requester %d not in any group\n", requester_id);
                return;
            }

            // Remove from old group (thread-safe)
            {
                std::lock_guard<std::mutex> lk(groups_mtx);
                _groups[*sender_gid].members.erase(requester_id);
            }

            // Find coordinator (master_mac) for requested_gid and notify it.
            auto it = _groups.find(requested_gid);
            if (it != _groups.end() && !it->second.master_mac.empty()) {
                Endpoint coord_ep(Address::from_string(it->second.master_mac), _control_local.port);
                std::string body = std::to_string(requester_id) + "|" + std::to_string(requested_gid); // body = requester_id|requested_gid
                send_control(_control_local, coord_ep, ControlType::GROUP_MOVE_NOTIFY, 0, body);
                printf("[GROUP] Forwarded move: VM %d -> group %d\n", requester_id, requested_gid);
            }
        }

        else if (ctrl == ControlType::GROUP_MOVE_NOTIFY) {

            int requester_id = -1;
            int requested_gid = -1;

            auto pos = msg.body.find('|'); // we expect body = "vm_id|gid"
            if (pos == std::string::npos) {
                printf("[GROUP] MOVE_NOTIFY malformed body: %s\n", msg.body.c_str());
                return;
            }

            try {
                requester_id = std::stoi(msg.body.substr(0, pos));
                requested_gid = std::stoi(msg.body.substr(pos + 1));
            } catch(...) {
                printf("[GROUP] MOVE_NOTIFY parse error: %s\n", msg.body.c_str());
                return;
            }

            if (requested_gid != current_group_id) {
                printf("[GROUP] MOVE_NOTIFY sent to wrong coordinator! requester=%d wanted group=%d but this is group=%d\n",
                    requester_id, requested_gid, current_group_id);
                return;
            }

            // Insert incoming VM in group list (thread-safe)
            {
                std::lock_guard<std::mutex> lk(groups_mtx);
                _groups[current_group_id].members.insert(requester_id);
            }

            // Tell VM it is now part of this group
            Endpoint vm_req = endpoint_from_vm(requester_id, _control_local.port);
            send_control(_control_local, vm_req, ControlType::GROUP_ASSIGN, 0, std::to_string(current_group_id)); // inform joining VM of the new group
            printf("[GROUP] Coordinator added VM %d to group %d\n", requester_id, current_group_id);
        }

        else if (ctrl == ControlType::GROUP_ASSIGN) {
            int new_gid = -1;
            try { new_gid = std::stoi(msg.body); } catch(...) { new_gid = -1; }
            if (new_gid >= 0) {
                current_group_id = new_gid;
                printf("[GROUP] This VM (%s) assigned to group %d\n", nic->address().str().c_str(), current_group_id);
            } else {
                printf("[GROUP] GROUP_ASSIGN with invalid body: %s\n", msg.body.c_str());
            }
        }

        // Forward to port observers
        for (auto* po : portObservers_) {
            if (!po) continue;

            // Control messages always pass
            if (ctrl != ControlType::NONE) {
                po->on_control(ctrl, c.from, c.to);
                continue;
            }

            // Filter data messages by group
            auto sender_gid = static_cast<int>(msg.group_id);
            int local_gid = current_group_id;

            if (sender_gid == -1 || sender_gid != local_gid) {
                printf("Dropped msg from group %d (local %d)\n", sender_gid, local_gid);
                continue;  // observer is from another group, skip it (drop packet)
            }

            // Observer is from the same group, deliver only if port matches
            if (po->port() == c.to.port || po->port() == ANY_PORT) {
                po->on_packet(c.from, c.to, c.payload.data(), c.length, c.origin);
            }
        }
    }

    void set_running_ptp(bool value) { this->_running_ptp = value; }
    void set_probabilistic_ptp_timeout(bool value) { this->_probabilistic_ptp_timeout = value; }

    bool get_running_ptp() { return this->_running_ptp; }
    bool get_probabilistic_ptp_timeout() { return this->_probabilistic_ptp_timeout; }

private:

    // Group management
    mutable std::mutex groups_mtx;
    struct GroupInfo {
        int group_id;
        std::set<int> members;  // store VM ids
        std::string master_mac; // optional
    };

    int current_group_id{0};
    std::unordered_map<int, GroupInfo> _groups;

    // Sync management
    int _sync_total_vms{0};
    Endpoint _control_local{};
    std::set<std::string> _sync_ready_nodes;
    std::function<void()> _sync_done_callback;
    bool _sync_go_sent = false;
    bool _sync_go_received = false;

    NIC* nic;
    ProtocolNumber etherType_;
    std::list<PortObserver*> portObservers_;

    ClockSyncer* _clock_syncer;
    bool _running_ptp;
    bool _probabilistic_ptp_timeout;
};

#endif // PROTOCOL_H
