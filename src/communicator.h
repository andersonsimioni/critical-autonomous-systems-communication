#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <stdexcept>
#include <string>

#include "ethernet.h"
#include "protocol.h"

// packet origin
enum class ChannelOrigin : unsigned char { Ethernet = 0, SharedMemory = 1 };

template <typename TNIC>
class Communicator {
public:
    using ProtocolT = Protocol<TNIC>;
    using Endpoint = typename ProtocolT::Endpoint;
    using Address = Ethernet::Address;

    struct Rx {
        typename Protocol<TNIC>::Endpoint      from;
        typename Protocol<TNIC>::Endpoint      to;
        std::vector<uint8_t> payload;
        ChannelOrigin origin;
    };

    /// @brief Use -1 for observe all ports
    /// @param port 
    void set_up_port_observer(uint16_t port)
    {
        _obs = new PortObserverImpl(this, ChannelOrigin::Ethernet, port);
        _protocol->attach(_obs);
    }

    Communicator(ProtocolT* protocol, const Endpoint& local): _protocol(protocol), _local(local)
    {
        set_up_port_observer(local.port);
    }

    ~Communicator() {
        if(_protocol && _obs) _protocol->detach(_obs);
        delete _obs;
    }

    void print_rx(const Rx& rx) {
        std::string s(rx.payload.begin(), rx.payload.end());
        std::cout << s << std::endl;
    }

    // route the send
    // if mac == broadcast send through ethernet, else send shm
    int send(const Endpoint& to, const void* data, size_t len) {
        return _protocol->send(_local, to, data, (unsigned)len);
    }

    int send(const Endpoint& to, const std::string& s) {
        return send(to, s.data(), s.size());
    }

    Rx receive() {
        std::unique_lock<std::mutex> lk(_mtx);
        _cv.wait(lk, [&]{ return !_queue.empty(); });
        Rx r = std::move(_queue.front());
        _queue.pop();
        return r;
    }

    bool try_receive(Rx& out) {
        std::lock_guard<std::mutex> lk(_mtx);
        if(_queue.empty()) return false;
        out = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    Endpoint local() const { return _local; }

private:
    class PortObserverImpl : public ProtocolT::PortObserver {
    public:
        PortObserverImpl(Communicator* owner, ChannelOrigin origin, uint16_t port) : _owner(owner), _origin(origin), _port(port) {}
        uint16_t port() const override { return _port; }

        void on_packet(const Endpoint& from, const Endpoint& to, const uint8_t* data, unsigned len) override {
            Communicator::Rx rx;
            rx.from = from;
            rx.to = to;
            rx.payload.assign(data, data+len);
            rx.origin = _origin;
            
            _owner->notify(rx, _port);
            _owner->enqueue(std::move(rx));
        }
    private:
        Communicator*  _owner;
        ChannelOrigin  _origin;
        uint16_t       _port;
    };

    void enqueue(Rx&& r) {
        std::lock_guard<std::mutex> lk(_mtx);
        _queue.push(std::move(r));
        _cv.notify_one();
    }

//private:
public:
    ProtocolT* _protocol{nullptr};
    Endpoint   _local{};

    PortObserverImpl* _obs{nullptr};

    std::mutex              _mtx;
    std::condition_variable _cv;
    std::queue<Rx>          _queue;

private:
    Observed<Rx, uint16_t> observed_;
    Rx scratch_;

public:

    void attach(Observer<Rx, uint16_t>* o)  { observed_.attach(o);  }
    void detach(Observer<Rx, uint16_t>* o)  { observed_.detach(o);  }
    void notify(const Rx& rx, uint16_t port = 0) {
        scratch_ = rx;
        observed_.notify(port, &scratch_);
    }
};
