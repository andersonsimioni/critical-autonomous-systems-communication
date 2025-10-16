#pragma once
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include "utils.h"
#include "vehicle_sensor_codes.h"  // our enum with data types

// Super simple publish–subscribe system.
// Each component has its own PublishSubscriber
class PublishSubscriber {
public:
    using SubscriberId = std::string;
    using u64 = std::uint64_t;
    
    void add_capability(VehicleDataType type) { capabilities_.insert(type); }
    void remove_capability(VehicleDataType type) { capabilities_.erase(type); }
    // tell what data types this component can produce
    bool supports(VehicleDataType type) const { return capabilities_.count(type) > 0; }

    // someone wants to subscribe to a data type
    // who = subscriber origin in format of ADDRESS:PORT
    bool subscribe(VehicleDataType type, const SubscriberId& who, u64 period_us) {
        if (!supports(type) || period_us == 0) return false;

        auto& list = subs_[type];
        u64 subscribed_at_us = get_microseconds_now();

        // if already subscribed, just update timing
        for (auto& s : list) 
        {
            if (s.who == who) 
            {
                s.period_us   = period_us;
                s.next_due_us = subscribed_at_us + period_us;
                return true;
            }
        }

        // if it's new, add it
        list.push_back(Subscription{who, period_us, subscribed_at_us + period_us});
        return true;
    }

    // Check what items should receive data now
    // type = VehicleDataType wated to now whats items are to send data, if allTypes == false
    // allTypes = if true, get all pending subscribed items to send data
    std::vector<SubscriberId> get_due_subscribers(VehicleDataType type, bool allTypes) {
        std::vector<SubscriberId> due;

        // list of types to check
        std::vector<VehicleDataType> types_to_check;
        if (all_types) types_to_check.assign(capabilities_.begin(), capabilities_.end());
        else types_to_check.push_back(type);

        u64 now_us = get_microseconds_now();

        for (auto t : types_to_check) {
            // skip if not supported
            if (!supports(t)) continue;

            auto it = subs_.find(t);
            if (it == subs_.end()) continue;

            auto& list = it->second;
            for (auto& s : list) {
                // calculate according to period if its time to send
                if (now_us >= s.next_due_us) 
                {
                    due.push_back(s.who);
                    s.next_due_us += s.period_us;
                }
            }
        }
        return due;
    }

    // Blocks until the time of the next due subscriber, avoiding busy waiting.
    // It finds the smallest next_due_us among all active subscriptions
    // and sleep until that time.
    void wait_until_next_due() {
        u64 next_due = UINT64_MAX;
        u64 now_us = get_microseconds_now();

        for (auto& [type, list] : subs_) for (auto& s : list) if (s.next_due_us < next_due) next_due = s.next_due_us;

        if (next_due == UINT64_MAX) 
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return; // no subscriptions at all
        }

        if (next_due > now_us) std::this_thread::sleep_for(std::chrono::microseconds(next_due - now_us));
    }

private:
    struct Subscription 
    {
        SubscriberId who;
        u64          period_us;
        u64          next_due_us;
    };

    std::unordered_set<VehicleDataType> capabilities_;
    std::unordered_map<VehicleDataType, std::vector<Subscription>> subs_;
};