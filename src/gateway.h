#pragma once

#include "car_component.h"
#include "protocol.h"
#include "utils.h"

#include <cstdio>
#include <set>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <optional>
#include <cassert>

template <typename TNIC>
class Gateway : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Base::_comm;
    using Base::_protocol;
    using Base::_local;
    using Endpoint = typename Base::Proto::Endpoint;
    using Rx   = typename Base::CommunicatorT::Rx;
    using Port = typename Protocol<TNIC>::Port;
public:
    Gateway(uint16_t port): Base(port, "Gateway"), _port(port), _sync_done(false) {}

    virtual void initialize(bool is_master_node, int nodes_count) override
    {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());

        // Initialize communicators and protocol
        this->initialize_communicator(is_master_node, nodes_count);

        // Enable protocol-level sync (sends READY and waits for GO broadcast) passing a lambda
        _protocol->enable_sync(5, _local, [this]{
            notify_sync_done();
        });

        // Notify protocol that this node is ready
        _protocol->send_control(_local, typename Protocol<TNIC>::Endpoint(Ethernet::Address::BROADCAST(), _local.port), Protocol<TNIC>::ControlType::READY);
        printf("[SYNC] READY sent from VM %s:%u\n", _local.mac.str().c_str(), _local.port);

        // Wait for GO from protocol
        {
            std::unique_lock<std::mutex> lk(_sync_mtx);
            _sync_cv.wait(lk, [&]{ return _sync_done.load(); });
        }
        printf("[SYNC] Received GO. All VMs synced. Starting ticks.\n");
        printf("[CAR COMPONENT][%s] initialized! Ready to start\n", this->name().c_str());
    }

    // Called by Communicator when GO arrives
    void notify_sync_done() {
        _sync_done.store(true);
        _sync_cv.notify_all();
    }

protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 1000; }

    void on_receive(const Rx& rx, ChannelOrigin origin) override
    {
        //printf("[GATEWAY] Received message [%s]\n", rx.msg.body.c_str());
        
        if(rx.origin == ChannelOrigin::SharedMemory) {
            // Message from a local component: forward to other cars
            Base::forward_message(rx);
        }
        else if(rx.origin == ChannelOrigin::Ethernet) {
            // Message from another car: deliver to local components
            Base::fanout_message(rx);
        }
    }

    void on_tick() override {
        // Example periodic broadcast
        std::string msg = "PING";
        //_comm->send(_to_bcast, msg);
    }

    // Send message helpers


private:
    uint16_t _port;
    std::atomic<bool> _sync_done;
    std::mutex _sync_mtx;
    std::condition_variable _sync_cv;
    std::set<std::string> _sync_ready_nodes; // store MACs as strings
};

