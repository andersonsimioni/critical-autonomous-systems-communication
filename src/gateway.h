#pragma once

#include "car_component.h"
#include "utils.h"
#include <cstdio>
#include <set>

template <typename TNIC>
class Gateway : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    Gateway(uint16_t port) : Base(port, "Gateway") {}

    void sync_vms(int total_vms = 5)
    {
        std::set<std::string> ready_nodes;
        ready_nodes.insert(this->_my_mac); 
        bool started = false;

        // Send READY via broadcast
        this->_comm.send(this->_to_bcast, "READY");
        std::cout << "[*] Gateway enviou READY\n";
        
        // Wait others to send READY
        while (!started) {
            std::cout << "[*] VM AGUARDA OUTRAS VMS \n";
            auto rx = this->_comm.receive(); //bloqueante
            std::string payload(rx.payload.begin(), rx.payload.end());

            std::cout <<"[" << (rx.origin == ChannelOrigin::Ethernet ? "ETHERNET" : "SHM") << "] "
                    << rx.from.mac.str() << ":" << rx.from.port
                    << " -> " << rx.to.mac.str()   << ":" << rx.to.port
                    << "  len=" << rx.payload.size()
                    << "  payload=\"" << payload << "\""
                    << "  recv_time="<<get_microseconds_now()<<"\n";

            // If message is READY, add at list
            if (payload == "READY") {
                ready_nodes.insert(rx.from.mac.str());
                std::cout << "[SYNC] Recebido READY de " << rx.from.mac.str()
                              << " (" << ready_nodes.size() << "/" << total_vms << ")\n";

                    // If this is the fist to see all READY, send "GO"
                if ((int)ready_nodes.size() == total_vms) {
                    std::cout << "[SYNC] Todos prontos. Enviando GO...\n";
                    this->_comm.send(this->_to_bcast, "GO");
                }
            } 
            // If message is GO, all VMS are ready to start
            else if (payload == "GO") {
                std::cout << "[SYNC] Recebido GO. Sincronização concluída.\n";
                started = true;
            }
        }
    }

    virtual void initialize(bool is_master_node, int nodes_count) override
    {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());
        this->initialize_communicator(is_master_node, nodes_count);
        //this->sync_vms();
        printf("[CAR COMPONENT][%s] initialized! Ready to start\n", this->name().c_str());
    }

protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 1000; }

    void on_tick() override {
        this->_comm->send(this->_local, "ping");
    }

private:
};
