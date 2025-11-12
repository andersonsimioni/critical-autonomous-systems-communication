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
#include <variant>
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

public:
    using VehicleValue = std::variant<int, double, std::string, bool>;
    struct VehicleData {
        VehicleDataType type;
        VehicleValue value;

        VehicleData(VehicleDataType t, VehicleValue v) : type(t), value(std::move(v)) {}
    };

    // Convert custom data to string
    std::string vehicle_data_to_string(const VehicleData& d) {
        if (std::holds_alternative<int>(d.value)) return std::to_string(std::get<int>(d.value));
        if (std::holds_alternative<double>(d.value)) return std::to_string(std::get<double>(d.value));
        if (std::holds_alternative<std::string>(d.value)) return std::get<std::string>(d.value);
        if (std::holds_alternative<bool>(d.value)) return std::get<bool>(d.value) ? "true" : "false";
        return "<unknown>";
    }

public:
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
        Protocol<NIC>::Endpoint toBcast    { Ethernet::Address::BROADCAST(), GATEWAY_PORT };

        this->_local    = me;
        this->_to_gate  = my_gateway;
        this->_to_bcast = toBcast;
        
        // Communicator, route dst = local mac to shm and dst = broadcast to ethernet
        this->_comm = new Communicator<NIC>(_protocol, me);
        _comm->attach(this);
        this->_comm->set_up_port_observer(is_master_node ? -1 : this->port());

        if (is_master_node) _ethernet_engine->start();
        _shm_engine->start();
    }

    // Parent forks; child runs the worker threads
    virtual void initialize(bool is_master_node, int nodes_count) {
        printf("[CAR COMPONENT][%s] initializing..\n", this->name().c_str());
        initialize_communicator(is_master_node, nodes_count);
        printf("[CAR COMPONENT][%s] initialized!\n", this->name().c_str());
      
        pthread_create(&_pubsub_thread, nullptr, pubsub_thread_entry, this);

        this->on_tick_loop();
    }

    void set_peer_ports(const std::vector<uint16_t>& ports) {
        _peer_ports.clear();
        for (auto port : ports) {
            if (port != _port) _peer_ports.push_back(port); // skip self
        }

        // Debug print
        printf("[DEBUG][%s] Peer ports:\n", _name.c_str());
        for (auto p : _peer_ports) {
            printf("  PORT %d\n", p);
        }
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
            _pubSub.wait_until_next_due();

            for (auto& [type, data] : _data) {
                auto due = _pubSub.get_due_subscribers(type, false);
                for (auto& who : due) {
                    printf("[SEND] to %s -> %d = ", who.c_str(), (int)type);
                    send_local("TO=" + who + " DATA=" + vehicle_data_to_string(data));
                }
            }
        }
    }

    // ---------------- Convenience Send Helpers ----------------

    // Send a string with format "TO=<port> DATA=<payload>"
    int send_local(const std::string& s) {
        size_t pos_to   = s.find("TO=");
        size_t pos_data = s.find("DATA=");

        if (pos_to == std::string::npos || pos_data == std::string::npos) {
            // Fallback: send entire string to gateway
            return _comm->send(_to_gate, static_cast<int>(_port), s.data(), s.size());
        }

        // Extract the port number
        std::string port_str = s.substr(pos_to + 3, pos_data - pos_to - 4); // 4 = " DATA"
        uint16_t port = static_cast<uint16_t>(std::stoi(port_str));

        // Extract the data
        std::string payload = s.substr(pos_data + 5); // 5 = strlen("DATA=")

        // Send to the parsed endpoint
        Protocol<NIC>::Endpoint dst { _my_mac, port };
        return _comm->send(dst, static_cast<int>(_port), payload.data(), payload.size());
    }

    void forward_message(const Rx& rx) {
        auto fwd_msg = rx.msg;
        fwd_msg.orig_vm   = _local.mac.addr[5];
        fwd_msg.orig_port = _port;
        uint64_t send_time = get_microseconds_now();
        fwd_msg.timestamp = send_time;
        _comm->send_message(_to_bcast, fwd_msg);
    }

    void fanout_message(const Rx& rx) {
        auto fwd_msg = rx.msg;
        fwd_msg.orig_vm   = _local.mac.addr[5];
        fwd_msg.orig_port = _port;
        uint64_t send_time = -1;
        for (auto port : _peer_ports) {
            Protocol<NIC>::Endpoint dst { _my_mac, port };
            send_time = get_microseconds_now();
            fwd_msg.timestamp = send_time;
            _comm->send_message(dst, fwd_msg);
        }
    }

    uint16_t port() const { return _port; }
    const std::string& name() const { return _name; }

protected:

    pthread_t* receive_msg_thread;

    Ethernet::Address _my_mac;
    EngineShm* _shm_engine;
    EngineEthernet* _ethernet_engine;
    NIC* _nic;
    Protocol<NIC>* _protocol;
    Communicator<NIC>* _comm;

    Endpoint _local{}, _to_gate{}, _to_bcast{};
    std::vector<uint16_t> _peer_ports;

    std::string _name;
    uint16_t _port{0};

    PublishSubscriber _pubSub;
    pthread_t _pubsub_thread;
    std::unordered_map<VehicleDataType, VehicleData> _data;

    bool digest_subs(const Rx& rx) {
        std::string s = rx.msg.body;
        size_t p1 = s.find("SUBS=");
        size_t p2 = s.find("SUBS_TYPE=");
        size_t p3 = s.find("SUBS_PERIOD=");
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return false;

        size_t e1 = s.find(' ', p1); if (e1 == std::string::npos) e1 = s.size();
        size_t e2 = s.find(' ', p2); if (e2 == std::string::npos) e2 = s.size();
        size_t e3 = s.find(' ', p3); if (e3 == std::string::npos) e3 = s.size();

        std::string addr  = s.substr(p1 + 5, e1 - (p1 + 5));
        VehicleDataType dtype = static_cast<VehicleDataType>(
            std::stoi(s.substr(p2 + 10, e2 - (p2 + 10)))
        );
        uint64_t period = std::stoull(s.substr(p3 + 12, e3 - (p3 + 12)));

        if(_pubSub.supports(dtype)) _pubSub.subscribe(dtype, addr, period);

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
