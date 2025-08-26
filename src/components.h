#pragma once

#include "car_component.h"
#include <cstdio>

// ---- Powertrain ----
// Periodically publishes RPM to broadcast (Ethernet) and listens for control.

template <typename TNIC>
class PowertrainComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    PowertrainComponent(typename Base::CommunicatorT* comm, uint16_t port)
    : Base(comm, port, "Powertrain") {}

protected:
    bool wants_tick() const override { return true; }
    unsigned tick_period_ms() const override { return 2000; } // every 2s

    void on_tick() override {
        _rpm = (_rpm + 300) % 6000;
        char msg[64];
        std::snprintf(msg, sizeof(msg), "rpm=%u torque=%u", _rpm, 250u);
        Base::send_broadcast(msg, std::strlen(msg));
    }

    void on_receive(const Rx& rx) override {
        // Example: accept commands addressed to this port (from gateway or others)
        auto s = Base::to_string(rx.payload);
    }

private:
    unsigned _rpm{900};
};

// ---- Brake ECU ----
// Listens for hazard or brake commands; acknowledges locally on shared memory

template <typename TNIC>
class BrakeComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    BrakeComponent(typename Base::CommunicatorT* comm, uint16_t port)
    : Base(comm, port, "Brake") {}

protected:
    void on_receive(const Rx& rx) override {
        auto s = Base::to_string(rx.payload);
        if (s == "brake!" || s == "hazard!") {
            Base::send_local("ack-brake", 9);
        }
    }
};

// ---- Gateway ECU ----
// Bridges between Ethernet and SHM using Communicator routing rules.
// Policy: forward SHM→Ethernet (broadcast) and Ethernet→SHM (local fanout) at same port.

template <typename TNIC>
class GatewayComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    GatewayComponent(typename Base::CommunicatorT* comm, uint16_t port)
    : Base(comm, port, "Gateway") {}

protected:
    void on_rx(const Rx& rx) override {
        if (rx.origin == ChannelOrigin::SharedMemory) {
            // From local components → broadcast to fleet
            Base::send_broadcast(rx.payload.data(), rx.payload.size());
        } else { // Ethernet
            // From other vehicles → deliver to local car (SHM)
            Base::send_local(rx.payload.data(), rx.payload.size());
        }
    }
};