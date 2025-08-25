#ifndef OBSERVER_H
#define OBSERVER_H

#include <list>
#include <mutex>
#include <condition_variable>

// -----------------------------------------------------
// Basic Observer interface
// -----------------------------------------------------
template <typename T, typename C = void>
class Observer {
public:
    virtual void update(C c, T* data) = 0;
};

// -----------------------------------------------------
// Observed subject
// -----------------------------------------------------
template <typename T, typename C = void>
class Observed {
public:
    void attach(Observer<T,C>* o) { observers.push_back(o); }
    void detach(Observer<T,C>* o) { observers.remove(o); }

    void notify(C c, T* d) {
        for(auto* o : observers) o->update(c,d);
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
    void update(C, D* d) {
        { std::lock_guard<std::mutex> lock(mtx); data.push_back(*d); }
        cv.notify_one();
    }
    D updated() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock,[&]{return !data.empty();});
        D d=data.front(); data.pop_front(); return d;
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
    void notify(C c, D* d){ for(auto* o:observers) o->update(c,d); }
private:
    std::list<ConcurrentObserver<D,C>*> observers;
};

#endif // OBSERVER_H
