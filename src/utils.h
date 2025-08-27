#ifndef UTILS_H
#define UTILS_H

#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/time.h>
#include <vector>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sstream>

// depend on your protocol and NIC
#include "protocol.h"

// -----------------------------------------------------
// Printer: simple PortObserver that logs incoming packets with timestamp
// -----------------------------------------------------
template <typename TNIC>
class Printer : public Protocol<TNIC>::PortObserver {
public:
    using Proto = Protocol<TNIC>;
    using Endpoint = typename Proto::Endpoint;

    explicit Printer(uint16_t dstPort) : dstPort_(dstPort) {}

    uint16_t port() const override { return dstPort_; }

    void on_packet(const Endpoint& from,
                   const Endpoint& to,
                   const uint8_t* data,
                   unsigned len) override
    {
        // get timestamp now
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm = *std::localtime(&t);

        // format HH:MM:SS.mmm
        std::ostringstream ts;
        ts << std::put_time(&tm, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();

        // payload string
        std::string s(reinterpret_cast<const char*>(data), len);

        std::cout << "[" << ts.str() << "] RX from " << from.mac.str()
                  << " srcPort=" << from.port
                  << " -> dstPort=" << to.port
                  << " len=" << len
                  << " data=\"" << s << "\"\n";
    }

private:
    uint16_t dstPort_;
};

// -----------------------------------------------------
// PeriodicSender: schedule periodic sends without busy loops
// -----------------------------------------------------
template <typename TNIC>
class PeriodicSender {
public:
    using Proto = Protocol<TNIC>;
    using Endpoint = typename Proto::Endpoint;

    PeriodicSender(Proto* proto,
                   Endpoint from,
                   Endpoint to,
                   std::string payload,
                   unsigned period_ms)
    : proto_(proto), from_(from), to_(to), payload_(std::move(payload))
    {
        period_us_ = static_cast<unsigned long>(period_ms) * 1000UL;
        instance_ = this;
        std::signal(SIGALRM, PeriodicSender::on_alarm);
        arm();
    }

    static void on_alarm(int) {if(instance_) instance_->tick();}

    void tick() {
        proto_->send(from_, to_, payload_.data(), static_cast<unsigned>(payload_.size()));
        std::cout << "[TX] \"" << payload_ << "\" to broadcast port=" << to_.port << "\n";
        arm();
    }

private:
    void arm() {
        struct itimerval t{};
        t.it_value.tv_sec = period_us_ / 1000000UL;
        t.it_value.tv_usec = period_us_ % 1000000UL;
        setitimer(ITIMER_REAL, &t, nullptr);
    }

    static PeriodicSender* instance_;
    Proto*       proto_;
    Endpoint     from_;
    Endpoint     to_;
    std::string  payload_;
    unsigned long period_us_;
};

template <typename TNIC>
PeriodicSender<TNIC>* PeriodicSender<TNIC>::instance_ = nullptr;


uint64_t get_microseconds_now()
{
    struct timeval aux;
    gettimeofday(&aux, NULL);
    uint64_t microseconds = (uint64_t)aux.tv_sec * 1000000 + aux.tv_usec;
    return microseconds;
}

#endif // UTILS_H
