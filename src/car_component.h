#ifndef CAR_COMPONENT_H
#define CAR_COMPONENT_H
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "engine.h"
#include "ethernet.h"
#include "engine_ethernet.h"
#include "engine_shm.h"
#include "communicator.h"
#include "nic.h"
#include "protocol.h"

static constexpr NetProtocolType ETYPE = 0x123;
static constexpr uint16_t PORT = 123;

// One fork per component. Inside the child we spawn two threads:
// - RX thread: blocks on receive() and calls on_receive()
// - TX thread: runs on_tick() every tick_period_ms() if wants_tick() == true
// Parent just manages the child PID.

template <typename TNIC>
class CarComponent : public Observer<typename Communicator<TNIC>::Rx, uint16_t> {
public:
    using Proto         = Protocol<TNIC>;
    using Endpoint      = typename Proto::Endpoint;
    using Address       = typename Proto::Address;
    using CommunicatorT = Communicator<TNIC>;
    using Rx = typename CommunicatorT::Rx;

    CarComponent(uint16_t my_port, std::string name) : _name(std::move(name)), _port(my_port) {}

    virtual ~CarComponent() { stop(); }

    void update(uint16_t port, Rx* rx) override { on_receive(*rx); }

    void on_tick_loop()
    {
        while (this->wants_tick())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(this->tick_period_ms()));
            this->on_tick();
        }
    }

    virtual void initialize_communicator(bool is_master_node, int nodes_count)
    {
        printf("initializing communicator..\n");

        this->_ethernet_engine = new EngineEthernet("eth0");
        this->_shm_engine = new EngineShm("/shared_region_23984293", is_master_node, nodes_count);
        this->_my_mac = this->_ethernet_engine->mac();

        this->_nic = new NIC(is_master_node ? this->_ethernet_engine : NULL, this->_shm_engine, this->_my_mac);
        this->_protocol = new Protocol<NIC>(_nic, ETYPE);

        Protocol<NIC>::Endpoint me      { this->_my_mac, PORT };
        Protocol<NIC>::Endpoint toBcast { Ethernet::Address::BROADCAST(), PORT };

        this->_local    = me;
        this->_to_bcast = toBcast;
        
        // Communicator, route dst = local mac to shm and dst = broadcast to ethernet
        this->_comm = new Communicator<NIC>(_protocol, me);
                
        this->_comm->set_up_port_observer(is_master_node ? -1 : this->port());


        if (is_master_node)
        {
            _ethernet_engine->start();
        }
        // _ethernet_engine->start();
        _shm_engine->start();
    }

    // Parent forks; child runs the worker threads
    virtual void initialize(bool is_master_node, int nodes_count) {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());
        initialize_communicator(is_master_node, nodes_count);
        printf("[CAR COMPONENT][%s] initialized!\n", this->name().c_str());
        
        this->on_tick_loop();
    }

    // Send SIGTERM and reap the child
    void stop() {
        if (_pid <= 0) return;
        ::kill(_pid, SIGTERM);
        ::waitpid(_pid, nullptr, 0);
        _pid = -1;
    }

    // Convenience send helpers
    int send_broadcast(const void* p, size_t n){ return _comm->send(_to_bcast, p, n); }
    int send_broadcast(const std::string& s)   { return send_broadcast(s.data(), s.size()); }
    
    int send_local(const void* p, size_t n)    { return _comm->send(_local, p, n); }
    int send_local(const std::string& s)       { return send_local(s.data(), s.size()); }

    uint16_t port() const { return _port; }
    const std::string& name() const { return _name; }

protected:
    // App-specific hooks
    virtual void on_receive(const typename CommunicatorT::Rx& rx) { printf("message arrived\n"); }
    virtual bool wants_tick() { return true; }
    virtual unsigned tick_period_ms() { return 1000; }
    virtual void on_tick() {}

    static std::string to_string(const std::vector<uint8_t>& v){
        return {v.begin(), v.end()};
    }

public:
//protected:
    pthread_t* receive_msg_thread;

    Ethernet::Address _my_mac;
    EngineShm* _shm_engine;
    EngineEthernet* _ethernet_engine;
    NIC* _nic;
    Protocol<NIC>* _protocol;
    Communicator<NIC>* _comm;

    Endpoint _local{}, _to_bcast{};
    std::string _name;
    uint16_t _port{0};

private:
    // Minimal SIGTERM handler to stop child threads cleanly
    static CarComponent*& self() { static CarComponent* s=nullptr; return s; }
    static void sigterm_handler(int){ if (self()) self()->_running.store(false); }
    void install_sigterm(){
        self() = this;
        struct sigaction sa{};
        sa.sa_handler = sigterm_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        ::sigaction(SIGTERM, &sa, nullptr);
    }

private:
    pid_t _pid{-1};              // child PID (owned by parent)
    std::atomic<bool> _running{false}; // only used in child
};

#endif // CAR_COMPONENT_H
