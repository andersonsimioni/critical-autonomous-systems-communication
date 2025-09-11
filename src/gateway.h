#pragma once

#include "car_component.h"
#include "utils.h"
#include <cstdio>

template <typename TNIC>
class Gateway : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    Gateway(uint16_t port) : Base(port, "Gateway") {}

    void sync_vms()
    {

    }

    virtual void initialize(bool is_master_node, int nodes_count) override
    {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());
        this->initialize_communicator(is_master_node, nodes_count);
        this->sync_vms();
        printf("[CAR COMPONENT][%s] initialized!\n", this->name().c_str());
    }

protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 1000; }

    void on_tick() override {
        this->_comm->send(this->_local, "ping");
    }

private:
};
