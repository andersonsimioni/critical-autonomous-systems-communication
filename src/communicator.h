#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <stdexcept>
#include <string>
#include <fstream>
#include <sstream>
#include <atomic>
#include <iostream>
#include <cerrno>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ethernet.h"
#include "protocol.h"
#include "utils.h"

template <typename TNIC> class Gateway;
template <typename TNIC>
class Communicator {
public:
    using ProtocolT = Protocol<TNIC>;
    using Endpoint = typename ProtocolT::Endpoint;
    using Address = Ethernet::Address;

    struct Rx {
        typename Protocol<TNIC>::Message msg;
        typename Protocol<TNIC>::Endpoint from;
        typename Protocol<TNIC>::Endpoint to;
        ChannelOrigin origin;
        uint64_t ReceiveTimeStampUs;
    };

    Communicator(ProtocolT* protocol, const Endpoint& local): _protocol(protocol), _local(local)
    {
        // Mount a host-shared log directory
        system("mkdir -p /mnt/logs && mount -t 9p -o trans=virtio hostshare /mnt/logs");

        // Open a log file per local endpoint (MAC and port-based name)
        std::ostringstream fname;
        uint8_t vm_id = _local.mac.addr[5];
        fname << "vm_" << static_cast<int>(vm_id) << "_" << local.port << ".log";

        std::string full_path = std::string("/mnt/logs/") + fname.str();
        _log.open(full_path, std::ios::out | std::ios::app);

        if (!_log.is_open()) {
            throw std::runtime_error("Failed to open log file: " + fname.str());
        } else {
            printf("[LOG] Successfully opened\n");
        }
    }

~Communicator() {
    if (_protocol) {
        if (_eth_obs) { _protocol->detach(_eth_obs); delete _eth_obs; _eth_obs = nullptr; }
        if (_shm_obs) { _protocol->detach(_shm_obs); delete _shm_obs; _shm_obs = nullptr; }
    }
    if (_log.is_open()) _log.close();
}

    // Optional: set the parent object (e.g., Gateway) to notify on sync events
    template <typename TParent>
    void set_parent(TParent* parent) { _parent = parent; }

    void set_up_port_observer(uint16_t port)
    {
        _eth_obs = new PortObserverImpl(this, ChannelOrigin::Ethernet, port);
        _protocol->attach(_eth_obs);

        _shm_obs = new PortObserverImpl(this, ChannelOrigin::SharedMemory, port);
        _protocol->attach(_shm_obs);
    }

    // Prepare a message according to protocol
    int send(const Endpoint& to, int type, const void* data, size_t len) {
        uint64_t send_time = get_microseconds_now();
        uint64_t id = _next_msg_id.fetch_add(1);
        uint8_t vm_id = _local.mac.addr[5];  // last byte of MAC

        // Construct Protocol::Message
        typename ProtocolT::Message msg;
        msg.orig_vm = vm_id;
        msg.orig_port = _local.port;
        msg.timestamp = send_time;
        msg.type = type;
        msg.msg_id = id;
        msg.body = std::string(reinterpret_cast<const char*>(data), len);

        // Serialize using Protocol::build_message
        std::string payload = _protocol->build_message(msg);

        // Log send
        logf("[SEND VM=%d PORT=%d TIME=%lu TYPE=%d ID=%lu]\n", vm_id, _local.port, send_time, type, id);

        return _protocol->send(_local, to, reinterpret_cast<const uint8_t*>(payload.data()), static_cast<unsigned>(payload.size()));
    }

    int send(const Endpoint& to, int type, const std::string& s) {
        return send(to, type, s.data(), s.size());
    }

    // send a full Protocol::Message (preserves msg_id, timestamp, type, body)
    int send_message(const Endpoint& to, const typename ProtocolT::Message& msg) {
        std::string payload = _protocol->build_message(msg);
        
        logf("[SEND VM=%d PORT=%d TIME=%lu TYPE=%d ID=%lu]\n", msg.orig_vm, msg.orig_port, msg.timestamp, msg.type, msg.msg_id);

        return _protocol->send(_local, to, reinterpret_cast<const uint8_t*>(payload.data()), static_cast<unsigned>(payload.size()));
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
        PortObserverImpl(Communicator* owner, ChannelOrigin origin, uint16_t port) : ProtocolT::PortObserver(origin), _owner(owner), _port(port) {}
        uint16_t port() const override { return _port; }

        void on_packet(const Endpoint& from, const Endpoint& to,
                    const uint8_t* data, unsigned len,
                    ChannelOrigin origin_of_packet) override
        {
            if (this->origin != origin_of_packet) return;

            uint64_t recv_time = get_microseconds_now();

            // Copy raw payload to vector
            std::vector<uint8_t> raw(data, data + len);

            // Parse Protocol::Message
            typename Protocol<TNIC>::Message msg = _owner->_protocol->parse_message(raw);

            // Clean up nulls and whitespace in body
            msg.body.erase(std::find(msg.body.begin(), msg.body.end(), '\0'), msg.body.end());
            msg.body.erase(std::remove_if(msg.body.begin(), msg.body.end(), ::isspace), msg.body.end());

            // Extract metadata for logging
            int vm_id = msg.orig_vm;
            int src_port = msg.orig_port;
            int type = msg.type;
            uint64_t id = msg.msg_id;

            _owner->logf("[RECV VM=%d PORT=%d TIME=%lu TYPE=%d ID=%lu]\n",
                        vm_id, src_port, recv_time, type, id);

            // Deliver to Rx
            Communicator::Rx rx;
            rx.msg = msg;
            rx.from = from;
            rx.to = to;
            rx.origin = origin_of_packet;
            rx.ReceiveTimeStampUs = recv_time;

            Communicator::Rx rx_copy = rx;     // copy before moving
            _owner->enqueue(std::move(rx));    // queue consumes the original
            _owner->notify(rx_copy, _port, origin_of_packet); // observers get a copy
        }

        void on_control(typename Protocol<TNIC>::ControlType type,
                        const Endpoint& from,
                        const Endpoint& to) override
        {
            switch(type) {
                case Protocol<TNIC>::ControlType::READY:
                    break;
                case Protocol<TNIC>::ControlType::GO:
                    if(_owner->_parent) {
                        static_cast<Gateway<TNIC>*>(_owner->_parent)->notify_sync_done();
                    }
                    break;

                default:
                    // ignore other control messages
                    break;
            }
        }

    private:
        Communicator*  _owner;
        uint16_t       _port;
    };

    void enqueue(Rx&& r) {
        std::lock_guard<std::mutex> lk(_mtx);
        _queue.push(std::move(r));
        _cv.notify_one();
    }

    void log_line(const std::string& line) {
        std::lock_guard<std::mutex> lk(_log_mtx);
        if (_log.is_open()) {
            _log << line << "\n";
            _log.flush();
        }
    }

    void logf(const char* fmt, uint64_t t, uint64_t id) {
        char buf[256];
        snprintf(buf, sizeof(buf), fmt, t, id);
        std::string line(buf);
        // print to screen (optional)
        //printf("%s\n", line.c_str());
        // write to log file
        log_line(line);
    }

    template<typename... Args>
    void logf(const char* fmt, Args... args) {
        char buf[256];
        snprintf(buf, sizeof(buf), fmt, args...);
        std::string line(buf);
        // print to screen (optional)
        //printf("%s\n", line.c_str());
        // write to log file
        if (_log.is_open()) {
            _log << line << "\n";
            _log.flush();
        }
    }

private:
    Observed<Rx, uint16_t> observed_;
    Rx scratch_;
    std::atomic<uint64_t> _next_msg_id{0};
    std::ofstream _log;
    void* _parent{nullptr};

public:
    void attach(Observer<Rx, uint16_t>* o)  { observed_.attach(o);  }
    void detach(Observer<Rx, uint16_t>* o)  { observed_.detach(o);  }
    void notify(const Rx& rx, uint16_t port, ChannelOrigin origin) {
        Rx copy = rx;  // make a local copy
        observed_.notify(port, &copy, origin);
    }

    ProtocolT* _protocol{nullptr};
    Endpoint   _local{};
    PortObserverImpl* _eth_obs{nullptr};
    PortObserverImpl* _shm_obs{nullptr};

    std::mutex              _mtx;
    std::mutex              _log_mtx;
    std::condition_variable _cv;
    std::queue<Rx>          _queue;
};
