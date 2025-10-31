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

// ---- Steering ----
template <typename TNIC>
class SteeringComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    SteeringComponent(uint16_t port) : Base(port, "Steering") {}
protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 1000; }
    void on_tick() override {
        int angle = (_angle + 5) % 360;
        _angle = angle;
        char msg[64];
        std::snprintf(msg, sizeof(msg), "steering_angle=%d", angle);
        Base::send_broadcast(msg, std::strlen(msg));
    }
    void on_receive(const Rx& rx, ChannelOrigin origin) override {
        if (rx.msg.body.find("set_angle=") == 0) {
            int new_angle = std::stoi(rx.msg.body.substr(10));
            _angle = new_angle;
            Base::send_local("ack-steering");
        }
    }
private:
    int _angle{0};
};

// ---- Transmission ----
template <typename TNIC>
class TransmissionComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    TransmissionComponent(uint16_t port) : Base(port, "Transmission") {}
protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 3000; }
    void on_tick() override {
        const char* gears[] = {"P", "R", "N", "D"};
        _gear = (_gear + 1) % 4;
        std::string msg = std::string("gear=") + gears[_gear];
        Base::send_broadcast(msg.c_str(), msg.size());
    }
    void on_receive(const Rx& rx, ChannelOrigin origin) override {
        if (rx.msg.body.find("set_gear=") == 0) {
            char g = rx.msg.body[9];
            switch (g) {
                case 'P': _gear = 0; break;
                case 'R': _gear = 1; break;
                case 'N': _gear = 2; break;
                case 'D': _gear = 3; break;
            }
            Base::send_local("ack-gear");
        }
    }
private:
    int _gear{0};
};

// ---- Airbag ----
template <typename TNIC>
class AirbagComponent : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    AirbagComponent(uint16_t port) : Base(port, "Airbag") {}
protected:
    bool wants_tick() override { return true; }
    unsigned tick_period_ms() override { return 5000; }
    void on_tick() override {
        Base::send_local("airbag_diag_ok");
    }
    void on_receive(const Rx& rx, ChannelOrigin origin) override {
        if (rx.msg.body == "collision!") {
            Base::send_local("airbag_deployed");
        } else if (rx.msg.body == "diagnostic?") {
            Base::send_local("airbag_diag_ok");
        }
    }
};