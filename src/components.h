#pragma once

#include "car_component.h"
#include "utils.h"
#include <cstdio>



// ---- Powertrain ----
// Periodically publishes RPM to broadcast (Ethernet) and listens for control.

template <typename TNIC>
class PowertrainComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Base::_comm;
    using Base::_protocol;
    using Base::_local;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    PowertrainComponent(uint16_t port) : Base(port, "Powertrain") {}

protected:

    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 2000; } // every 2s

    void on_tick() override {
        /* _rpm = (_rpm + 300) % 6000;
        char msg[64];
        std::snprintf(msg, sizeof(msg), "time=%llu rpm=%u torque=%u", get_microseconds_now(), _rpm, 250u);
        Base::send_broadcast(msg, std::strlen(msg)); */
        //Base::send_local("Hello");
    }

    void on_receive(const Rx& rx, ChannelOrigin origin) override {

    }

private:
    unsigned _rpm{900};
};

// ---- Brake ECU ----
// Listens for hazard or brake commands; acknowledges locally on shared memory

template <typename TNIC>
class BrakeComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Base::_comm;
    using Base::_protocol;
    using Base::_local;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    BrakeComponent(uint16_t port)
    : Base(port, "Brake") {}

protected:

    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 2000; } // every 2s

    void on_receive(const Rx& rx, ChannelOrigin origin) override {

        //printf("[BRAKE] Brake component received message [%s]\n", rx.msg.body.c_str());
        if (rx.msg.body == "brake!" || rx.msg.body == "hazard!") {
            std::string ack = "ack-brake";
            Base::send_local(ack);
        }
    }

    void on_tick() override {
        std::string payload = "brake!";

        // Send to local gateway
        Base::send_local(payload);
    }
};