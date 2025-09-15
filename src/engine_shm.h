#pragma once
// EngineShm: header-only shared-memory Engine
// - Inherits Engine
// - Matches Engine API exactly:
//     int send(const Ethernet::Frame&) override;
//     int receive(Ethernet::Frame&)   override;
// - Broadcast fan-out across processes via SysV SHM
// - Observer/exception via POSIX signal (default SIGUSR1)

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <csignal>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>

#include "engine.h"
#include "ethernet.h"

class NIC;

#ifndef _SEMUN_DEFINED
#define _SEMUN_DEFINED
union semun { int val; struct semid_ds* buf; unsigned short* array; struct seminfo* __buf; };
#endif

// Tunables
static constexpr uint32_t SHM_MAX_CONSUMERS = 16;
static constexpr uint32_t SHM_QUEUE_SLOTS = 64;

// SHM layout: store the whole Ethernet::Frame verbatim
struct ShmSlot {
    uint16_t used;             // 0 = empty, 1 = has frame
    Ethernet::Frame frame;     // copy-by-value (blob)
};

struct ConsumerQueue {
    ShmSlot  slots[SHM_QUEUE_SLOTS];
    uint32_t rd{0}, wr{0};
    pid_t    pid{0};           // 0 = free
};

struct ShmBus {
    uint32_t     magic{0x5353484D}; // 'SSHM'
    ConsumerQueue q[SHM_MAX_CONSUMERS];
};

class EngineShm : public Engine {
public:
    EngineShm(const char* shm_region_name, bool is_master, int nodes);

    ~EngineShm() override {
        _running.store(false);
    }

    // -------- Engine interface ----------
    int send(const Ethernet::Frame& frame) override;
    int start() override;

    // -------- Raw Fallback ---------------
    using RawRxHandler = std::function<void(const char* data, size_t len)>;
    void set_raw_rx_handler(RawRxHandler h) { _raw_rx = std::move(h); }

    int send_bytes(const void* data, size_t len);

protected:
    void on_receive_msg(int msg_len, char* msg) override;

private:
    // Registration
    bool register_me() {
        bus_lock();
        for (unsigned i = 0; i < SHM_MAX_CONSUMERS; ++i) {
            auto& q = _bus->q[i];
            if (q.pid == 0) {
                q.pid = getpid();
                q.rd = q.wr = 0;
                _me = (int)i;
                bus_unlock();
                return true;
            }
        }
        bus_unlock();
        return false;
    }

    void unregister_me() {
        if (_bus && _me >= 0) {
            bus_lock();
            auto& q = _bus->q[_me];
            if (q.pid == getpid()) { q.pid = 0; q.rd = q.wr = 0; }
            bus_unlock();
            _me = -1;
        }
    }

    // Ring buffer
    static uint32_t next(uint32_t x){ return (x + 1) % SHM_QUEUE_SLOTS; }

    static bool enqueue(ConsumerQueue& q, const Ethernet::Frame& f) {
        uint32_t n = next(q.wr);
        if (n == q.rd) return false; // full -> drop
        q.slots[q.wr].frame = f;     // copy whole frame
        q.slots[q.wr].used = 1;
        q.wr = n;
        return true;
    }

    static bool dequeue(ConsumerQueue& q, Ethernet::Frame& f) {
        if (q.rd == q.wr) return false;      // empty
        if (!q.slots[q.rd].used) return false;
        f = q.slots[q.rd].frame;             // copy whole frame
        q.slots[q.rd].used = 0;
        q.rd = next(q.rd);
        return true;
    }

    // Detach
    void detach() { if (_bus) { shmdt((void*)_bus); _bus = nullptr; } }

    // Semaphores
    int  sem_q(unsigned i) const { return 1 + (int)i; } // per-queue counter
    void bus_lock()   { sem_add(0, -1); }               // bus mutex
    void bus_unlock() { sem_add(0, +1); }
    void sem_add(int semnum, int delta) {
        struct sembuf op{ (unsigned short)semnum, (short)delta, 0 };
        while (semop(_sem, &op, 1) == -1 && errno == EINTR) { /* retry */ }
    }

    

private:
    std::atomic<bool> _running{false};
    RawRxHandler _raw_rx;

    const char* _shm_name;
    bool  _is_master;
    int   _nodes;
};

#endif // ENGINE_SHM_H
