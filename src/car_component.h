#ifndef CAR_COMPONENT_H
#define CAR_COMPONENT_H

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <thread>

#include "communicator.h"
#include "nic.h"
#include "protocol.h"

// Base class for any ECU/component process
// It owns a receive loop bound to a specific logical port
// Subclasses implement on_receive() and (periodic) on_transmit()

template <typename TNIC>
class CarComponent {
public:
    using Proto = Protocol<TNIC>;
    using Endpoint = typename Proto::Endpoint;
    using Address = typename Proto::Address;
    using CommunicatorT = Communicator<TNIC>;

    CarComponent(CommunicatorT* comm, uint16_t my_port, std::string name)
    : _comm(comm), _name(std::move(name)), _port(my_port)
    {
        if(!_comm) throw std::runtime_error("CarComponent: null Communicator");
        _local = _comm->local();
        // We stick to the same port. Broadcast MAC for Ethernet, local MAC for SHM path
        _to_bcast = Endpoint{ Address::BROADCAST(), _port };
        _to_local = Endpoint{ _local.mac, _port };
    }

    virtual ~CarComponent() { stop(); }

    void start() {
        _running = true;
        _rx_thread = std::thread([this]{ this->receive_loop(); });
        if (wants_tick()) {
            _tx_thread = std::thread([this]{ this->transmit_loop(); });
        }
    }

    void stop() {
        _running = false;
        if(_rx_thread.joinable()) _rx_thread.join();
        if(_tx_thread.joinable()) _tx_thread.join();
    }

    // Sends via communicator
    int send_broadcast(const void* data, size_t len) {
        return _comm->send(_to_bcast, data, len);
    }
    int send_broadcast(const std::string& s) { return send_broadcast(s.data(), s.size()); }

    int send_local(const void* data, size_t len) {
        return _comm->send(_to_local, data, len);
    }
    int send_local(const std::string& s) { return send_local(s.data(), s.size()); }

    uint16_t port() const { return _port; }
    const std::string& name() const { return _name; }

protected:
    // Called for every received packet that matches the port
    virtual void on_receive(const typename CommunicatorT::Rx& rx) = 0;

    // Periodic work (like sensors). Return period in ms.
    virtual bool wants_tick() const { return false; }
    virtual unsigned tick_period_ms() const { return 1000; }
    virtual void on_tick() {} // default: no transmission

    // Utility: convert payload to string
    static std::string to_string(const std::vector<uint8_t>& v) {
        return std::string(v.begin(), v.end());
}

private:
    void receive_loop() {
        while(_running) {
            auto rx = _comm->receive();
            // Deliver only packets that target our port
            if (rx.to.port == _port) {
                on_receive(rx);
            }
        }
    }

    void transmit_loop() {
        const unsigned period = tick_period_ms();
        while(_running) {
            on_tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(period));
        }
    }

protected:
    CommunicatorT* _comm;
    Endpoint _local{};
    Endpoint _to_bcast{};
    Endpoint _to_local{};
    std::string _name;
    uint16_t _port{0};

private:
    std::atomic<bool> _running{false};
    std::thread _rx_thread;
    std::thread _tx_thread;
};

#endif // car_component
