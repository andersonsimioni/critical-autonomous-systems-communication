#pragma once
#ifndef ENGINE_SHM_H
#define ENGINE_SHM_H

#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <cstring>
#include <stdexcept>

#include "nic.h"
#include "engine.h"
#include "shm_node.h"
#include "ethernet.h"

class NIC; // forward

class EngineShm : public Engine, public ShmNode {
public:
    EngineShm(char* shm_region_name, bool is_master, int nodes);

    ~EngineShm() override {
        _running.store(false);
    }

    // -------- Engine interface ----------
    int send(const Ethernet::Frame& frame) override;
    int start() override;

    // -------- Raw Fallback ---------------
    using RawRxHandler = std::function<void(const char* data, size_t len)>;
    void set_raw_rx_handler(RawRxHandler h) { _raw_rx = std::move(h); }

    int send_bytes(const void* data, size_t len);

protected:
    void on_receive_msg(int msg_len, char* msg) override;

private:
    // Serialize frame to bytes
    static std::vector<char> serialize_frame(const Ethernet::Frame& f);

    // Deserialize bytes on frame
    static bool deserialize_frame(const char* data, size_t len, Ethernet::Frame& out);

private:
    std::atomic<bool> _running{false};
    RawRxHandler _raw_rx;

    char* _shm_name;
    bool  _is_master;
    int   _nodes;
};

#endif // ENGINE_SHM_H
