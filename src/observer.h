#ifndef OBSERVER_H
#define OBSERVER_H

#include <list>
#include <mutex>
#include <condition_variable>

// packet origin
enum class ChannelOrigin : unsigned char { Ethernet = 0, SharedMemory = 1 };

// -----------------------------------------------------
// Basic Observer interface
// -----------------------------------------------------
template <typename T, typename C = void>
class Observer {
public:
    virtual void update(C c, T* data, ChannelOrigin origin) = 0;
};

// -----------------------------------------------------
// Observed subject
// -----------------------------------------------------
template <typename T, typename C = void>
class Observed {
public:
    void attach(Observer<T,C>* o) { observers.push_back(o); }
    void detach(Observer<T,C>* o) { observers.remove(o); }

    void notify(C c, T* d, ChannelOrigin origin) {
        for(auto* o : observers) o->update(c,d,origin);
    }
private:
    std::list<Observer<T,C>*> observers;
};

// -----------------------------------------------------
// Concurrent Observer (thread-safe)
// -----------------------------------------------------
template <typename D, typename C = void>
class ConcurrentObserver {
public:
    void update(C c, D* d, ChannelOrigin origin) {
        std::lock_guard<std::mutex> lock(mtx);
        data.push_back({c, *d, origin});
        cv.notify_one();
    }

    struct Entry {
        C channel;
        D value;
        ChannelOrigin origin;
    };

    Entry updated() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]{ return !data.empty(); });
        Entry e = data.front(); 
        data.pop_front(); 
        return e;
    }

private:
    std::list<D> data;
    std::mutex mtx;
    std::condition_variable cv;
};

// -----------------------------------------------------
// Concurrent Observed subject
// -----------------------------------------------------
template <typename D, typename C = void>
class ConcurrentObserved {
public:
    void attach(ConcurrentObserver<D,C>* o){ observers.push_back(o); }
    void detach(ConcurrentObserver<D,C>* o){ observers.remove(o); }
    void notify(C c, D* d, ChannelOrigin origin){
        for(auto* o: observers) 
            o->update(c, d, origin);
    }
private:
    std::list<ConcurrentObserver<D,C>*> observers;
};

#endif // OBSERVER_H
