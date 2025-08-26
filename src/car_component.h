#ifndef CAR_COMPONENT_H
#define CAR_COMPONENT_H
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "communicator.h"
#include "nic.h"
#include "protocol.h"

template <typename TNIC>
class CarComponent {
public:
    using Proto        = Protocol<TNIC>;
    using Endpoint     = typename Proto::Endpoint;
    using Address      = typename Proto::Address;
    using CommunicatorT= Communicator<TNIC>;

    CarComponent(CommunicatorT* comm, uint16_t my_port, std::string name)
    : _comm(comm), _name(std::move(name)), _port(my_port) {
        if(!_comm) throw std::runtime_error("CarComponent: null Communicator");
        _local    = _comm->local();
        _to_bcast = Endpoint{ Address::BROADCAST(), _port };
        _to_local = Endpoint{ _local.mac, _port };
    }

    virtual ~CarComponent() { stop(); }

    // create a child proccess that run the default rotine to send and receive data
    void start() {
        if (_pid > 0) return;
        _pid = ::fork();
        if (_pid < 0) throw std::runtime_error("CarComponent: fork() failed");

        if (_pid == 0) 
        {
            //child process rotine
            install_handlers();
            if (wants_tick()) arm_itimer(tick_period_ms());

            while (running()) {
                auto rx = _comm->receive();
                if (tick_fired()) on_tick();
                if (rx.to.port == _port) on_receive(rx);
            }
            _exit(0);
        }
        
        //parent process jump direct to here
    }

    void stop() {
        if (_pid <= 0) return;
        ::kill(_pid, SIGTERM);
        ::waitpid(_pid, nullptr, 0);
        _pid = -1;
    }

    int send_broadcast(const void* p, size_t n){ return _comm->send(_to_bcast, p, n); }
    int send_broadcast(const std::string& s)   { return send_broadcast(s.data(), s.size()); }
    int send_local(const void* p, size_t n)    { return _comm->send(_to_local, p, n); }
    int send_local(const std::string& s)       { return send_local(s.data(), s.size()); }

    uint16_t port() const { return _port; }
    const std::string& name() const { return _name; }

protected:
    //callback when receive data
    virtual void on_receive(const typename CommunicatorT::Rx& rx) = 0;

    virtual bool wants_tick() const { return false; }
    virtual unsigned tick_period_ms() const { return 1000; }
    virtual void on_tick() {}

    static std::string to_string(const std::vector<uint8_t>& v){
        return {v.begin(), v.end()};
    }

protected:
    CommunicatorT* _comm;
    Endpoint _local{}, _to_bcast{}, _to_local{};
    std::string _name;
    uint16_t _port{0};

private:
    //control child options
    static volatile std::sig_atomic_t& run_flag() { static volatile std::sig_atomic_t f=1; return f; }
    static volatile std::sig_atomic_t& tick_flag(){ static volatile std::sig_atomic_t f=0; return f; }
    static void on_sigterm(int){ run_flag() = 0; }
    static void on_sigalrm(int){ tick_flag() = 1; }
    static inline bool running(){ return run_flag()!=0; }
    static inline bool tick_fired(){ if(tick_flag()){ tick_flag()=0; return true; } return false; }

    static void install_handlers(){
        struct sigaction sa{};
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = on_sigterm; ::sigaction(SIGTERM, &sa, nullptr);
        sa.sa_handler = on_sigalrm; ::sigaction(SIGALRM, &sa, nullptr);
    }

    static void arm_itimer(unsigned ms){
        itimerval it{};
        it.it_value.tv_sec  = ms/1000; it.it_value.tv_usec  = (ms%1000)*1000;
        it.it_interval      = it.it_value;
        ::setitimer(ITIMER_REAL, &it, nullptr);
    }

private:
    pid_t _pid{-1};
};

#endif // CAR_COMPONENT_H
