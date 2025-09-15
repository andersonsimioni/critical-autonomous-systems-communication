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
    using Address = Ethernet::Address;
    using Protocol_Number = Ethernet::Protocol;

    EngineShm() = default;
    ~EngineShm() override { unregister_me(); detach(); }

    // Create or join the SHM bus
    bool open(const char* path, int proj_id) {
        key_t k = ftok(path, proj_id);
        if (k == (key_t)-1) return false;

        // SHM segment
        int created = 0;
        _shm = shmget(k, sizeof(ShmBus), IPC_CREAT | IPC_EXCL | 0600);
        if (_shm == -1) {
            if (errno != EEXIST) return false;
            _shm = shmget(k, sizeof(ShmBus), 0600);
            if (_shm == -1) return false;
        } else created = 1;

        _bus = (ShmBus*)shmat(_shm, nullptr, 0);
        if (_bus == (void*)-1) { _bus = nullptr; return false; }
        if (created) { *_bus = ShmBus{}; }
        if (_bus->magic != 0x5353484D) return false;

        // Semaphores: [0] = bus mutex, [1..N] = per-queue item counters
        _sem = semget(k, 1 + SHM_MAX_CONSUMERS, IPC_CREAT | IPC_EXCL | 0600);
        if (_sem == -1) {
            if (errno != EEXIST) return false;
            _sem = semget(k, 1 + SHM_MAX_CONSUMERS, 0600);
            if (_sem == -1) return false;
        } else {
            union semun u{};
            for (unsigned i = 0; i < 1 + SHM_MAX_CONSUMERS; ++i) { u.val = 0; semctl(_sem, i, SETVAL, u); }
            u.val = 1; semctl(_sem, 0, SETVAL, u); // bus mutex unlocked
        }

        return register_me();
    }

    void bindNIC(NIC* nic_ptr) { _nic = nic_ptr; }
    const Address& address() const { return _addr; }
    void address(const Address& a) { _addr = a; }

    void setNotifySignal(int signo) { _sig = signo; }

    //send ethernet frame through shm
    int send(const Ethernet::Frame& frame) override {
        if (!_bus || _me < 0) return -1;

        bus_lock();
        int fanout = 0;
        for (unsigned i = 0; i < SHM_MAX_CONSUMERS; ++i) {
            if ((int)i == _me) continue;  // no loopback
            auto& q = _bus->q[i];
            if (q.pid == 0) continue;
            if (enqueue(q, frame)) {
                sem_add(sem_q(i), +1);   // item available
                fanout++;
                if (_sig > 0) ::kill(q.pid, _sig); // notify recipient process
            }
        }
        bus_unlock();
        return fanout; // number of queues that received the frame
    }

    int receive(Ethernet::Frame& out) {
        if (!_bus || _me < 0) return -1;

        //wait for one item in queue
        sem_add(sem_q(_me), -1);

        auto& q = _bus->q[_me];
        Ethernet::Frame f{};
        if (!dequeue(q, f)) return -1;

        out = f; // copy whole frame
        return 1; // success
    }


    //no use this, use signal to notify
    int start() { return 0; }

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
    int      _shm{-1};
    int      _sem{-1};
    ShmBus*  _bus{nullptr};
    int      _me{-1};
    Address  _addr{};       // optional local MAC if your NIC uses it
    NIC*     _nic{nullptr}; // back-pointer for compatibility
    int      _sig{SIGUSR1}; // POSIX signal for notification
};
