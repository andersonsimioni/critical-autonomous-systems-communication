#include "engine_shm.h"
#include <cstdio>

EngineShm::EngineShm(char* shm_region_name, bool is_master, int nodes)
: ShmNode(shm_region_name, is_master, nodes), _shm_name(shm_region_name), _is_master(is_master), _nodes(nodes)
{
    // do not initialize here, initialize on start() after create all nodes..
    // IMPORTANT: start the master node FIRST!!
}

int EngineShm::start() {
    initialize_node();
    _running.store(true);
    return 0;
}

int EngineShm::send(const Ethernet::Frame& frame) {
    auto blob = serialize_frame(frame);
    if (blob.empty()) return -1;
    if (blob.size() > PAYLOAD) return -2;
    return ShmNode::send_msg(static_cast<int>(blob.size()), blob.data()) ? 0 : -3;
}

int EngineShm::send_bytes(const void* data, size_t len) {
    if (!data || len == 0) return -1;
    if (len > PAYLOAD) return -2;
    return ShmNode::send_msg(static_cast<int>(len), (char*)data) ? 0 : -3;
}

// ---------------- Serialization ----------------
std::vector<char> EngineShm::serialize_frame(const Ethernet::Frame& f) {
    std::vector<char> out;
    size_t total = sizeof(f.dst) + sizeof(f.src) + sizeof(f.proto) + sizeof(f.size) + f.size;
    out.resize(total);

    char* ptr = out.data();
    std::memcpy(ptr, &f.dst, sizeof(f.dst)); ptr += sizeof(f.dst);
    std::memcpy(ptr, &f.src, sizeof(f.src)); ptr += sizeof(f.src);
    std::memcpy(ptr, &f.proto, sizeof(f.proto)); ptr += sizeof(f.proto);
    std::memcpy(ptr, &f.size, sizeof(f.size)); ptr += sizeof(f.size);
    if (f.size) std::memcpy(ptr, f.data, f.size);

    return out;
}

bool EngineShm::deserialize_frame(const char* data, size_t len, Ethernet::Frame& out) {
    if (!data || len < sizeof(out.dst)+sizeof(out.src)+sizeof(out.proto)+sizeof(out.size)) return false;
    const char* ptr = data;
    std::memcpy(&out.dst, ptr, sizeof(out.dst)); ptr += sizeof(out.dst);
    std::memcpy(&out.src, ptr, sizeof(out.src)); ptr += sizeof(out.src);
    std::memcpy(&out.proto, ptr, sizeof(out.proto)); ptr += sizeof(out.proto);
    std::memcpy(&out.size, ptr, sizeof(out.size)); ptr += sizeof(out.size);
    if (out.size > Ethernet::Frame::MAX_DATA) return false;
    if (sizeof(out.dst)+sizeof(out.src)+sizeof(out.proto)+sizeof(out.size)+out.size != len) return false;
    if (out.size) {
        std::memcpy(out.data, ptr, out.size);
    }
    return true;
}

// ---------------- Receive hook ----------------
void EngineShm::on_receive_msg(int msg_len, char* msg) {
    if (msg_len <= 0 || !msg) return;

    if (nic) {
        Ethernet::Frame f{};
        if (deserialize_frame(msg, static_cast<size_t>(msg_len), f)) {
            static_cast<NIC*>(nic)->on_frame(f);
            return;
        }
    }

    if (_raw_rx) {
        _raw_rx(msg, static_cast<size_t>(msg_len));
        return;
    }

    std::fprintf(stderr, "[EngineShm] RX %d bytes (sem NIC/handler)\n", msg_len);
}
