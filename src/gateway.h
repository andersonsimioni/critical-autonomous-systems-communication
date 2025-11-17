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
    Gateway(uint16_t port, int vm_id, int total_sync_vms, bool is_sync_master = false)
        : Base(port, "Gateway"), _port(port), _vm_id(vm_id), _total_sync_vms(total_sync_vms), _sync_master(is_sync_master), _sync_done(false) {}

    virtual void initialize(bool is_master_node, int total_nodes, int group_id) override
    {
        printf("[CAR COMPONENT][%s] initializing.. (VM %d%s)\n", this->name().c_str(), _vm_id, _sync_master ? " MASTER" : "");

        // Initialize communicators and protocol
        this->initialize_communicator(true, total_nodes);
        _protocol->set_current_group(group_id);
        this->register_with_coordinator(is_master_node); // broadcast group registration
        this->wait_for_ack_from_coordinator(is_master_node);

        // Enable protocol-level sync passing a lambda for GO arrival
        _comm->set_on_sync_done([this]() {this->notify_sync_done();});
        if(_sync_master) {_protocol->set_master(true);}
        _protocol->enable_start_sync(_total_sync_vms, _local, [this]{notify_sync_done();});

        // Only non-master nodes send READY; master just waits for READYs
        if (!_sync_master) {
            std::thread([this]() {
                while (!_sync_done.load()) {
                    _protocol->send_control(_local, Endpoint(Ethernet::Address::BROADCAST(), _local.port), Protocol<TNIC>::ControlType::READY);
                    //printf("[SYNC][VM %d] READY sent\n", _vm_id);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }).detach();
        }

        // Wait for GO broadcast from master
        {
            std::unique_lock<std::mutex> lk(_sync_mtx);
            _sync_cv.wait(lk, [&]{ return _sync_done.load(); });
        }
        //printf("[SYNC][VM %d] GO received. All VMs synced. Starting ticks.\n", _vm_id);
        //printf("[CAR COMPONENT][%s] initialized! Ready to start\n", this->name().c_str());
    }

    // Called by Communicator when GO arrives
    void notify_sync_done() {
        _sync_done.store(true);
        _sync_cv.notify_all();
    }

protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 10; } // 10 miliseconds in order to send enough sync messages

    void on_receive(const Rx& rx, ChannelOrigin origin) override
    {
        //printf("[GATEWAY] Received message [%s]\n", rx.msg.body.c_str());
        
        if(rx.origin == ChannelOrigin::SharedMemory) {
            // Message from a local component: forward to other cars
            Base::forward_message(rx, _vm_id);
        }
        else if(rx.origin == ChannelOrigin::Ethernet) {
            // Message from another car: deliver to local components
            Base::fanout_message(rx, _vm_id);
        }
    }

    void on_tick() override {

        std::string msg = "PING";
        uint64_t now_us = get_microseconds_now(); // wall-clock timestamp in microseconds

        Endpoint master_endpoint{Ethernet::Address::BROADCAST(), _local.port}; // endpoint for group coordinator, only it will respond

        // SYNC REQUEST every 3 seconds
        if (!_sync_master) {
            if (now_us - _last_sync_request_us.load() >= 3'000'000ULL) { // 3 seconds
                if(_protocol->get_running_ptp())
                {
                    _protocol->set_probabilistic_ptp_timeout(true);
                }
                else
                {
                    _protocol->set_running_ptp(true);
                    _protocol->set_probabilistic_ptp_timeout(false);
                    _last_sync_request_us.store(now_us);

                    //printf("[SYNC][VM %d] Sending SYNC_REQ to master\n", _vm_id);

                    // PROVISIORIO
                    //uint8_t msgac = _protocol->generate_mac(buf);

                    _protocol->send_control(_local, master_endpoint, Protocol<TNIC>::ControlType::SYNC_REQ);
                }
            }
        }

        // GROUP MOVE REQUEST - for testing at the moment
        static bool did_request = false;
        int new_group = 3;
        if(_vm_id == 1 && !did_request) {
            _protocol->move_group(new_group);
            did_request = true;
            printf("[DEBUG] VM %d requested to move to group %d.\n", _vm_id, new_group);
        }       
    }

private:
    uint16_t _port;
    int _group_id{0};                                       // VM group 
    int _vm_id{0};                                          // VM identity
    int _total_sync_vms;                                    // number of VMs to wait for at the barrier
    bool _sync_master;                                      // true if this Gateway coordinates READY/GO and SYNC
    std::atomic<bool> _sync_done;                           // flag for passing the initialization barrier
    std::mutex _sync_mtx;
    std::condition_variable _sync_cv;
    std::set<std::string> _sync_ready_nodes;                // store MACs as strings
    std::atomic<uint64_t> _last_sync_request_us{0};         // last time this VM synchronized
};

