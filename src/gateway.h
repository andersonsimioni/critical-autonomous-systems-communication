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

    virtual void default_rotine() override
    {
        printf("All done, car ready!\n");
        while (true)
        {
            sleep(1);
            printf("NIC MAC ON GATEWAY = %d %d %d %d %d %d\n", this->_comm->_protocol->nic->mac.addr[0], this->_comm->_protocol->nic->mac.addr[1], this->_comm->_protocol->nic->mac.addr[2], this->_comm->_protocol->nic->mac.addr[3], this->_comm->_protocol->nic->mac.addr[4], this->_comm->_protocol->nic->mac.addr[5]);
            this->_comm->send(this->_local, "ping");
        }
    }

    virtual void start(bool is_master_node, int nodes_count) override
    {
        printf("gateway starting..\n");
        this->initialize_communicator(is_master_node, nodes_count);
        sync_vms();
    }

protected:
    bool wants_tick() const override { return true; }
    unsigned tick_period_ms() const override { return 2000; } // every 2s

    void on_tick() override {
        
    }

    void on_receive(const Rx& rx) override {
        // Example: accept commands addressed to this port (from gateway or others)
        auto s = Base::to_string(rx.payload);
        std::cout<<"Gateway Received data: "<<s<<"\n";
    }

private:
};
