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

template <typename TNIC>
class Gateway : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Endpoint = typename Base::Proto::Endpoint;
    using Rx   = typename Base::CommunicatorT::Rx;
    using Port = typename Protocol<TNIC>::Port;
public:
    Gateway(uint16_t port)
        : Base(port, "Gateway"),
          _sync_done(false),
          _sync_observer(std::make_unique<SyncObserver>(this)) {}

    virtual void initialize(bool is_master_node, int nodes_count) override
    {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());

        // Initialize communicators and protocol
        this->initialize_communicator(is_master_node, nodes_count);

        // Attach the sync observer before enabling protocol-level sync
        this->_protocol->attach(_sync_observer.get());

        // Enable protocol-level sync (sends READY and waits for GO broadcast)
        this->_protocol->enable_sync(5, this->_local);

        // Notify protocol that this node is ready
        this->_protocol->send_control(
            this->_local,
            typename Protocol<TNIC>::Endpoint(Ethernet::Address::BROADCAST(), this->_local.port),
            Protocol<TNIC>::ControlType::READY);

        // Wait for GO from protocol
        {
            std::unique_lock<std::mutex> lk(_sync_mtx);
            _sync_cv.wait(lk, [&]{ return _sync_done.load(); });
        }
        printf("[SYNC] Received GO. All VMs synced. Starting ticks.\n");
        printf("[CAR COMPONENT][%s] initialized! Ready to start\n", this->name().c_str());
    }

protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 1000; }

    void on_receive(const Rx& rx, ChannelOrigin origin) override
    {
        std::string payload(rx.payload.begin(), rx.payload.end());
        printf("[DEBUG] Gateway receiving message. Origin: [%d], [%d]\n", int(origin), int(rx.origin));

        // Remove ID header if present
        if (payload.rfind("ID=", 0) == 0) {
            size_t space_pos = payload.find(' ');
            if (space_pos != std::string::npos) payload = payload.substr(space_pos + 1);
        }

        if (auto p = payload.find("TO="); p != std::string::npos) 
        {
            std::string to = payload.substr(p + 3, payload.find(' ', p) - (p + 3));
            Endpoint to_endpoint = Endpoint::endpoint_from_string(to);
            this->_comm->send(to_endpoint, payload);
        }


        if(rx.origin == ChannelOrigin::SharedMemory) {
            // Message from a local component: forward to other cars
            printf("[DEBUG] Received payload from shm, forwarding externally\n");
            this->_comm->send(this->_to_bcast, payload);
        }
        else if(rx.origin == ChannelOrigin::Ethernet) {
            // Message from another car: deliver to local components
            printf("[DEBUG] Received payload from Ethernet, forwarding internally\n");
            Base::send_fanout(payload);
        }
    }

    void on_tick() override {
        // Example periodic broadcast
        std::string msg = "PING";
        //this->_comm->send(this->_to_bcast, msg);
    }

private:
    // Called by SyncObserver when GO arrives
    void notify_sync_done() {
        _sync_done.store(true);
        _sync_cv.notify_all();
    }

    // Inner class to observe READY/GO control messages from Protocol
    class SyncObserver : public Protocol<TNIC>::PortObserver {
        Gateway<TNIC>* _parent;
    public:
        explicit SyncObserver(Gateway<TNIC>* parent)
            : Protocol<TNIC>::PortObserver(ChannelOrigin::Ethernet), _parent(parent)
        {
            printf("[DEBUG] SyncObserver constructed for port %d\n", _parent->port());
        }

        Port port() const override {
            return _parent->port(); // Observe this gateway's port
        }

        void on_packet(const typename Protocol<TNIC>::Endpoint& from,
                    const typename Protocol<TNIC>::Endpoint& to,
                    const uint8_t* data,
                    unsigned len,
                    ChannelOrigin origin) override {
            // Forward payload to parent if necessary
            typename Base::CommunicatorT::Rx msg;
            msg.from = from;
            msg.to = to;
            msg.origin = origin;
            msg.payload.assign(data, data + len);
            _parent->on_receive(msg, origin);
        }

        void on_control(typename Protocol<TNIC>::ControlType type,
                        const typename Protocol<TNIC>::Endpoint& from,
                        const typename Protocol<TNIC>::Endpoint& to) override
        {
            printf("[DEBUG] SyncObserver saw a message\n");
            if(type == Protocol<TNIC>::ControlType::GO || static_cast<int>(type) == 1){
                printf("[DEBUG] SyncObserver saw GO\n");
                _parent->notify_sync_done();
            }
        }
    };

    // Members for synchronization
    std::mutex _sync_mtx;
    std::condition_variable _sync_cv;
    std::atomic<bool> _sync_done;

    // Sync observer
    std::unique_ptr<SyncObserver> _sync_observer;
};

