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
        
        uint64_t SentTimeStampUs;
        uint64_t ReceiveTimeStampUs;
    };

    /// @brief Use -1 for observe all ports
    /// @param port 
    void set_up_port_observer(uint16_t port)
    {
        _eth_obs = new PortObserverImpl(this, ChannelOrigin::Ethernet, port);
        _protocol->attach(_eth_obs);

        _shm_obs = new PortObserverImpl(this, ChannelOrigin::SharedMemory, port);
        _protocol->attach(_shm_obs);
    }

    Communicator(ProtocolT* protocol, const Endpoint& local): _protocol(protocol), _local(local)
    {
        // Mount a host-shared log directory
        system("mkdir -p /mnt/logs && mount -t 9p -o trans=virtio hostshare /mnt/logs");

        // Open a log file per local endpoint (MAC and port-based name)
        std::ostringstream fname;
        const auto& mac = this->_local.mac.addr;
        uint16_t id = (static_cast<uint16_t>(mac[4]) << 8) | mac[5];
        fname << "vm_" << id << "_" << local.port << ".log";

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

    // route the send
    int send(const Endpoint& to, const void* data, size_t len) {
        uint64_t send_time = get_microseconds_now();
        uint64_t id = _next_msg_id.fetch_add(1);

        // Log
        logf("[SEND t=%lu id=%lu]\n", send_time, id);

        // Prepend ID header to payload
        std::string payload = 
            "TS=" + std::to_string(get_microseconds_now()) + " " +
            "ID=" + std::to_string(id) + " " + 
            std::string(reinterpret_cast<const char*>(data), len);

        return _protocol->send(_local, to, reinterpret_cast<const uint8_t*>(payload.data()), (unsigned)payload.size());
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
        PortObserverImpl(Communicator* owner, ChannelOrigin origin, uint16_t port) : ProtocolT::PortObserver(origin), _owner(owner), _port(port) {}
        uint16_t port() const override { return _port; }

        void on_packet(const Endpoint& from, const Endpoint& to, const uint8_t* data, unsigned len, ChannelOrigin origin_of_packet) override {

            // Filter by observer's expected origin
            if (this->origin != origin_of_packet) return;

            uint64_t recv_time = get_microseconds_now();
            std::string msg(reinterpret_cast<const char*>(data), len);

            // Extract ID if present
            long id = -1;
            uint64_t timestamp = -1;

            if (msg.rfind("TS=", 0) == 0) {
                size_t space_pos = msg.find(' ');
                if (space_pos != std::string::npos) {
                    timestamp = std::stol(msg.substr(3, space_pos - 3));
                    msg = msg.substr(space_pos + 1); // strip Timestamp
                }
            }

            if (msg.rfind("ID=", 0) == 0) {
                size_t space_pos = msg.find(' ');
                if (space_pos != std::string::npos) {
                    id = std::stol(msg.substr(3, space_pos - 3));
                    msg = msg.substr(space_pos + 1); // strip ID
                }
            }
            
            // Deliver message
            Communicator::Rx rx;
            rx.from = from;
            rx.to = to;
            rx.payload.assign(data, data+len);
            rx.origin = origin_of_packet;

            rx.SentTimeStampUs = timestamp;
            rx.ReceiveTimeStampUs = get_microseconds_now();

            // Log
            _owner->logf("[RECV t=%lu id=%ld]\n", recv_time, id);
            _owner->logf("[LATENCY t=%ld]\n", recv_time - timestamp);
            
            _owner->enqueue(std::move(rx));
            printf("[DEBUG] Communicator enqueueing message from [%d] origin\n", static_cast<int>(origin_of_packet));
            _owner->notify(rx, _port, origin_of_packet);
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
        // print to screen
        printf("%s\n", line.c_str());
        // also write to file
        log_line(line);
    }

    template<typename... Args>
    void logf(const char* fmt, Args... args) {
        char buf[256];
        snprintf(buf, sizeof(buf), fmt, args...);
        std::string line(buf);
        // print to screen
        printf("%s\n", line.c_str());
        // also write to file
        if (_log.is_open()) {
            _log << line << "\n";
            _log.flush();
        }
    }

public:
    ProtocolT* _protocol{nullptr};
    Endpoint   _local{};
    PortObserverImpl* _eth_obs{nullptr};
    PortObserverImpl* _shm_obs{nullptr};

    std::mutex              _mtx;
    std::mutex              _log_mtx;
    std::condition_variable _cv;
    std::queue<Rx>          _queue;

private:
    Observed<Rx, uint16_t> observed_;
    Rx scratch_;
    std::atomic<uint64_t> _next_msg_id{0};
    std::ofstream _log;

public:

    void attach(Observer<Rx, uint16_t>* o)  { observed_.attach(o);  }
    void detach(Observer<Rx, uint16_t>* o)  { observed_.detach(o);  }
    void notify(const Rx& rx, uint16_t port, ChannelOrigin origin) {
        Rx copy = rx;  // make a local copy
        observed_.notify(port, &copy, origin);
    }

};
