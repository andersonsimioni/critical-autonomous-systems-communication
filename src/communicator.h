#pragma once
// Minimal, header-only Communicator for the SO2 project.
// Works with a NIC that exposes: 
//   int send(const Ethernet::Address&, Ethernet::Protocol, const void*, size_t);
//   int receive(Ethernet::Address* src, Ethernet::Protocol* proto, void* out, size_t max);
// and an Engine underneath that notifies via POSIX signal (e.g., SIGUSR1).

#include <functional>
#include <atomic>
#include <csignal>
#include <cstring>
#include <unistd.h>

#include "ethernet.h" // must define Ethernet::Address (6 bytes) and Ethernet::Protocol

template <typename NicT>
class Communicator {
public:
    using Address = Ethernet::Address;
    using Protocol = Ethernet::Protocol;
    using RxCallback = std::function<void(const Address& src, Protocol proto, const uint8_t* data, size_t size)>;

    explicit Communicator(NicT& nic, Protocol proto): _nic(nic), _proto(proto) { }

    // Set broadcast address if your stack uses another convention
    void setBroadcast(const Address& b) { _bcast = b; }

    // Install signal-based observer (default SIGUSR1, same used by EngineShm)
    void installObserver(int signo = SIGUSR1) {
        _signo = signo;

        // Install handler once per process (idempotent for this class)
        struct sigaction sa{};
        sa.sa_handler = &Communicator::on_signal_static;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0; // keep it simple; avoid SA_RESTART here
        sigaction(_signo, &sa, nullptr);
    }

    // Register message callback. Called in the same thread that calls run()/handleOnce().
    void onMessage(RxCallback cb) { _cb = std::move(cb); }

    // Send helper (defaults to broadcast)
    int send(const void* data, size_t size, const Address& dst = broadcast()) {
        return _nic.send(dst, _proto, data, size);
    }

    // Blocking: wait for one notification and handle a single message.
    // Returns: bytes handled (>0), 0 if truncated, -1 on error.
    int handleOnce() {
        // Wait until a signal marks ready (very light wait loop).
        wait_for_signal();

        // Drain just ONE message (simple & safe). If multiple arrived, virão mais sinais.
        Address src{};
        Protocol pr{};
        uint8_t buf[_maxPayload]{};
        int n = _nic.receive(&src, &pr, buf, sizeof(buf));
        if (n >= 0 && _cb) _cb(src, pr, buf, (n>0 ? (size_t)n : 0));
        return n;
    }

    // Simple loop: waits for signals and handles messages one by one.
    // Break condition: stop() called from another thread or callback.
    void run() {
        _running = true;
        while (_running) {
            if (handleOnce() < 0) {
                // On error, you may choose to sleep a bit to avoid a busy loop.
                usleep(1000);
            }
        }
    }

    // Stop the loop (thread-safe).
    void stop() { _running = false; }

    // Expose current broadcast
    const Address& broadcast() const { return _bcast; }

private:
    // ---- lightweight signal wait ----
    static void on_signal_static(int) {
        // mark that at least one message is ready
        g_ready.store(true, std::memory_order_release);
    }

    void wait_for_signal() {
        // Fast path: if flag already set, consume it and continue.
        while (true) {
            if (g_ready.exchange(false, std::memory_order_acq_rel)) return;

            // Sleep briefly; in real systems you could use signalfd/ppoll for elegance.
            // Here we keep it simple and portable without extra syscalls.
            // NOTE: usleep is async-signal-safe to call outside the handler.
            usleep(500);
        }
    }

private:
    NicT&     _nic;
    Protocol  _proto;
    Address   _bcast{{0xff,0xff,0xff,0xff,0xff,0xff}};
    RxCallback _cb;

    int _signo{SIGUSR1};
    static inline std::atomic<bool> g_ready{false};
    static constexpr size_t _maxPayload = 1600; // should be >= NIC/Engine MTU
    std::atomic<bool> _running{false};
};
