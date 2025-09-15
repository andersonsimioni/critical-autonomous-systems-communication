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
        Endpoint      from;
        Endpoint      to;
        std::vector<uint8_t> payload;
        ChannelOrigin origin;
    };

    Communicator(ProtocolT* protoEth, ProtocolT* protoShm, const Endpoint& local)
        : _eth(protoEth), _shm(protoShm), _local(local)
    {
        if(!_eth || !_shm) throw std::runtime_error("Communicator: null protocol");

        _ethObs = new PortObserverImpl(this, ChannelOrigin::Ethernet, local.port);
        _shmObs = new PortObserverImpl(this, ChannelOrigin::SharedMemory, local.port);

        _eth->attach(_ethObs);
        _shm->attach(_shmObs);
    }

    ~Communicator() {
        if(_eth && _ethObs) _eth->detach(_ethObs);
        if(_shm && _shmObs) _shm->detach(_shmObs);
        delete _ethObs; delete _shmObs;
    }

    // route the send
    // if mac == broadcast send through ethernet, else send shm
    int send(const Endpoint& to, const void* data, size_t len) {
        if (to.mac == Address::BROADCAST()) {
            //std::cout<<"Sending broadcast msg: "<<data<<"\n";
            Endpoint bto = to;
            bto.mac = Address::BROADCAST();
            return _eth->send(_local, bto, data, (unsigned)len);
        }

        if (to.mac == _local.mac) 
        {
            return _shm->send(_local, to, data, (unsigned)len);
        }

        // fallback send broadcast ethernet
        Endpoint bto = to;
        bto.mac = Address::BROADCAST();
        //std::cout<<"Sending broadcast msg: "<<data<<"\n";
        return _eth->send(_local, bto, data, (unsigned)len);
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
        PortObserverImpl(Communicator* owner, ChannelOrigin origin, uint16_t port)
            : _owner(owner), _origin(origin), _port(port) {}
        uint16_t port() const override { return _port; }
        void on_packet(const Endpoint& from, const Endpoint& to,
                       const uint8_t* data, unsigned len) override {
            Communicator::Rx rx;
            rx.from = from;
            rx.to = to;
            rx.payload.assign(data, data+len);
            rx.origin = _origin;
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

private:
    ProtocolT* _eth{nullptr};
    ProtocolT* _shm{nullptr};
    Endpoint   _local{};

    PortObserverImpl* _ethObs{nullptr};
    PortObserverImpl* _shmObs{nullptr};

    std::mutex              _mtx;
    std::condition_variable _cv;
    std::queue<Rx>          _queue;
};
