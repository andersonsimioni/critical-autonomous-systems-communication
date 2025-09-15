#pragma once

#include "car_component.h"
#include "utils.h"
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
        std::snprintf(msg, sizeof(msg), "time=%llu rpm=%u torque=%u", get_microseconds_now(), _rpm, 250u);
        Base::send_broadcast(msg, std::strlen(msg));
    }

    void on_receive(const Rx& rx) override {
        // Example: accept commands addressed to this port (from gateway or others)
        auto s = Base::to_string(rx.payload);
        std::cout<<"Received data: "<<s<<"\n";
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