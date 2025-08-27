#ifndef CAR_COMPONENT_H
#define CAR_COMPONENT_H
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "communicator.h"
#include "nic.h"
#include "protocol.h"

// One fork per component. Inside the child we spawn two threads:
// - RX thread: blocks on receive() and calls on_receive()
// - TX thread: runs on_tick() every tick_period_ms() if wants_tick() == true
// Parent just manages the child PID.

template <typename TNIC>
class CarComponent {
public:
    using Proto         = Protocol<TNIC>;
    using Endpoint      = typename Proto::Endpoint;
    using Address       = typename Proto::Address;
    using CommunicatorT = Communicator<TNIC>;

    CarComponent(CommunicatorT* comm, uint16_t my_port, std::string name)
    : _comm(comm), _name(std::move(name)), _port(my_port) {
        if(!_comm) throw std::runtime_error("CarComponent: null Communicator");
        _local    = _comm->local();
        _to_bcast = Endpoint{ Address::BROADCAST(), _port };
        _to_local = Endpoint{ _local.mac, _port };
    }

    virtual ~CarComponent() { stop(); }

    // Parent forks; child runs the worker threads
    void start() {
        if (_pid > 0) return;               // already running
        _pid = ::fork();
        if (_pid < 0) throw std::runtime_error("CarComponent: fork() failed");

        if (_pid == 0) {
            // -------- child process --------
            install_sigterm();               // SIGTERM -> _running = false
            _running.store(true);

            std::thread rx_thread([this]{
                while (true/* _running.load() */) {
                    std::cout<<"waiting for data..\n";
                    auto rx = _comm->receive();          // should unblock on SIGTERM (EINTR) if implemented
                    std::cout<<"Received data!\n";
                    //if (!_running.load()) break;
                    /* if (rx.to.port == _port)  on_receive(rx);*/
                    on_receive(rx);
                    sleep(1000);
                }
            });

            std::thread tx_thread;
            //if (wants_tick()) {
                tx_thread = std::thread([this]{
                    while (true) {
                        on_tick();
                        sleep(tick_period_ms()/1000);
                    }
                });
            //}

            if (rx_thread.joinable()) rx_thread.join();
            if (tx_thread.joinable()) tx_thread.join();
            _exit(0);
        }
        // -------- parent returns here --------
    }

    // Send SIGTERM and reap the child
    void stop() {
        if (_pid <= 0) return;
        ::kill(_pid, SIGTERM);
        ::waitpid(_pid, nullptr, 0);
        _pid = -1;
    }

    // Convenience send helpers
    int send_broadcast(const void* p, size_t n){ return _comm->send(_to_bcast, p, n); }
    int send_broadcast(const std::string& s)   { return send_broadcast(s.data(), s.size()); }
    int send_local(const void* p, size_t n)    { return _comm->send(_to_local, p, n); }
    int send_local(const std::string& s)       { return send_local(s.data(), s.size()); }

    uint16_t port() const { return _port; }
    const std::string& name() const { return _name; }

protected:
    // App-specific hooks
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
    // Minimal SIGTERM handler to stop child threads cleanly
    static CarComponent*& self() { static CarComponent* s=nullptr; return s; }
    static void sigterm_handler(int){ if (self()) self()->_running.store(false); }
    void install_sigterm(){
        self() = this;
        struct sigaction sa{};
        sa.sa_handler = sigterm_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        ::sigaction(SIGTERM, &sa, nullptr);
    }

private:
    pid_t _pid{-1};              // child PID (owned by parent)
    std::atomic<bool> _running{false}; // only used in child
};

#endif // CAR_COMPONENT_H
