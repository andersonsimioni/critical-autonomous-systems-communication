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
#include "ports.h"
#include "publish_subscriber.h"

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

    void update(uint16_t port, Rx* rx, ChannelOrigin origin) override { on_receive(*rx, origin); }

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

        Protocol<NIC>::Endpoint me         { this->_my_mac, _port };
        Protocol<NIC>::Endpoint my_gateway { this->_my_mac, GATEWAY_PORT };
        Protocol<NIC>::Endpoint my_brake   { this->_my_mac, BRAKE_PORT };
        Protocol<NIC>::Endpoint toBcast    { Ethernet::Address::BROADCAST(), GATEWAY_PORT };

        this->_local    = me;
        this->_to_gate  = my_gateway;
        this->_to_brake = my_brake;
        this->_to_bcast = toBcast;
        
        // Communicator, route dst = local mac to shm and dst = broadcast to ethernet
        this->_comm = new Communicator<NIC>(_protocol, me);
                
        this->_comm->set_up_port_observer(is_master_node ? -1 : this->port());


        if (is_master_node) _ethernet_engine->start();
        _shm_engine->start();
    }

    // Parent forks; child runs the worker threads
    virtual void initialize(bool is_master_node, int nodes_count) {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());
        initialize_communicator(is_master_node, nodes_count);
        printf("[CAR COMPONENT][%s] initialized!\n", this->name().c_str());
        
        _pubSub = new PublishSubscriber();
        pthread_create(&_pubsub_thread, nullptr, pubsub_thread_entry, this);

        this->on_tick_loop();
    }

    // Send SIGTERM and reap the child
    void stop() {
        if (_pid <= 0) return;
        ::kill(_pid, SIGTERM);
        ::waitpid(_pid, nullptr, 0);
        _pid = -1;
    }

    static void* pubsub_thread_entry(void* arg) 
    {
        auto* self = static_cast<CarComponent*>(arg);
        self->publisher_loop();
        return nullptr;
    }

    // locking loop
    void publisher_loop() {
        while (true) 
        {
            pubsub.wait_until_next_due();

            for (auto& [type, data] : _data) {
                auto due = pubsub.get_due_subscribers(type, false);
                for (auto& who : due) {
                    printf("[SEND] to %s -> %d = ", who.c_str(), (int)type);
                    send_fanout("TO=" + std::to_string(due) + " DATA=" + std::to_string(data));
                }
            }
        }
    }


    // Convenience send helpers
    int send_broadcast(const void* p, size_t n){ return _comm->send(_to_bcast, p, n); }
    int send_broadcast(const std::string& s)   { return send_broadcast(s.data(), s.size()); }
    
    int send_local(const void* p, size_t n)    { return _comm->send(_to_gate, p, n); }
    int send_local(const std::string& s)       { return send_local(s.data(), s.size()); }

    int send_fanout(const void* p, size_t n)    {
        printf("[DEBUG] Fanning out message [%s]\n", p);
        return _comm->send(_to_brake, p, n);
    }
    int send_fanout(const std::string& s)       { return send_fanout(s.data(), s.size()); }

    uint16_t port() const { return _port; }
    const std::string& name() const { return _name; }

protected:

    static bool digest_subs(const Rx& rx) {
        std::string s(rx.payload.begin(), rx.payload.end());
        size_t p1 = s.find("SUBS=");
        size_t p2 = s.find("SUBS_TYPE=");
        size_t p3 = s.find("SUBS_PERIOD=");
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return false;

        size_t e1 = s.find(' ', p1); if (e1 == std::string::npos) e1 = s.size();
        size_t e2 = s.find(' ', p2); if (e2 == std::string::npos) e2 = s.size();
        size_t e3 = s.find(' ', p3); if (e3 == std::string::npos) e3 = s.size();

        std::string addr  = s.substr(p1 + 5, e1 - (p1 + 5));
        VehicleDataType dtype = std::stoi(s.substr(p2 + 10, e2 - (p2 + 10)));
        u64 period = std::stoull(s.substr(p3 + 12, e3 - (p3 + 12)));


        if(this->_pubSub.supports(dtype)) this->_pubSub.subscribe(dtype, addr, period);

        return true;
    }

    // App-specific hooks
    virtual void on_receive(const typename CommunicatorT::Rx& rx, ChannelOrigin origin) {
        // Default behavior
        printf("[CarComponent] Received packet on port %u from %s\n", rx.from.port, origin == ChannelOrigin::Ethernet ? "Ethernet" : "SharedMemory");
        this->digest_subs(rx);
        
    }
    virtual bool wants_tick() { return true; }
    virtual unsigned tick_period_ms() { return 1000; }
    virtual void on_tick() {}

    static std::string to_string(const std::vector<uint8_t>& v){
        return {v.begin(), v.end()};
    }

public:
    using VehicleValue = std::variant<int, double, std::string, bool>;
    struct VehicleData {
        VehicleDataType type;
        VehicleValue value;

        VehicleData(VehicleDataType t, VehicleValue v) : type(t), value(std::move(v)) {}
    };

    pthread_t* receive_msg_thread;

    Ethernet::Address _my_mac;
    EngineShm* _shm_engine;
    EngineEthernet* _ethernet_engine;
    NIC* _nic;
    Protocol<NIC>* _protocol;
    Communicator<NIC>* _comm;

    Endpoint _local{}, _to_gate{}, _to_brake{}, _to_bcast{};
    std::string _name;
    uint16_t _port{0};

    PublishSubscriber _pubSub;
    pthread_t _pubsub_thread;
    std::unordered_map<VehicleDataType, VehicleData> _data;

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
