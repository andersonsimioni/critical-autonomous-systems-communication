#pragma once

#include "car_component.h"
#include "utils.h"
#include <cstdio>
#include <set>
#include <string>

template <typename TNIC>
class Gateway : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    Gateway(uint16_t port) : Base(port, "Gateway") {}

    virtual void initialize(bool is_master_node, int nodes_count) override
    {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());

        sem_init(&this->sem_sync, 0, 0);

        // Initialize communicators and protocol
        this->initialize_communicator(is_master_node, nodes_count);

        // Enable protocol-level sync
        this->_protocol->enable_sync(5, this->_local);

        // Wait for GO message via Communicator queue
        sem_wait(&this->sem_sync);
        printf("[SYNC] Received GO. All VMs synced. Starting ticks.\n");

        printf("[CAR COMPONENT][%s] initialized! Ready to start\n", this->name().c_str());
    }

protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 1000; }

    void on_receive(const Rx& rx) override
    {
        std::string payload(rx.payload.begin(), rx.payload.end());

        // Remove ID header if present
        if (payload.rfind("ID=", 0) == 0) {
            size_t space_pos = payload.find(' ');
            if (space_pos != std::string::npos) payload = payload.substr(space_pos + 1);
        }

        if (payload == "GO") sem_post(&this->sem_sync);
    }

    void on_tick() override {
        // Build payload
        std::string msg = "PING";

        // Send to broadcast
        this->_comm->send(this->_to_bcast, msg);
        //this->_comm->send(this->_local, msg);
    }

private:
    sem_t sem_sync;
};
