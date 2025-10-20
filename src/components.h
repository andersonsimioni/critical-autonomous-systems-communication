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
    BrakeComponent(uint16_t port)
    : Base(port, "Brake") {}

protected:

    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 5000; } // every 5s

    void on_receive(const Rx& rx, ChannelOrigin origin) override {
        auto s = Base::to_string(rx.payload);
        printf("[DEBUG] Brake component received a message!\n");
        if (s == "brake!" || s == "hazard!") {
            std::string ack = "ack-brake";
            Base::send_local(ack);
        }
    }

    void on_tick() override {
        std::string payload = "brake!";

        // Send 10 messagens for testing
        for (int i = 0; i <= 10; i++) {        
            
            printf("[DEBUG] Brake component tick %d \n", i);
            // Send to local gateway
            Base::send_local(payload);
        }

    }
};