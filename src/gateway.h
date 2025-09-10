#pragma once

#include "car_component.h"
#include "utils.h"
#include <cstdio>

template <typename TNIC>
class Gateway : public CarComponent<TNIC> {
    using Base = CarComponent<TNIC>;
    using Rx = typename Base::CommunicatorT::Rx;
public:
    Gateway(uint16_t port) : Base(port, "Gateway") {}

protected:
    bool wants_tick() const override { return true; }
    unsigned tick_period_ms() const override { return 2000; } // every 2s

    void on_tick() override {
        
    }

    void on_receive(const Rx& rx) override {
        // Example: accept commands addressed to this port (from gateway or others)
        auto s = Base::to_string(rx.payload);
        std::cout<<"Gateway Received data: "<<s<<"\n";
    }

private:
};
