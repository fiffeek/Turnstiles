#ifndef SRC_TURNSTILE_H_
#define SRC_TURNSTILE_H_

#include <type_traits>
#include <cstddef>
#include <mutex>
#include <cassert>
#include <thread>
#include <queue>
#include <condition_variable>

struct Turnstile {
    size_t waiting;
    std::mutex *locker;
    std::condition_variable cv;
    bool goThrough;

    Turnstile();
    ~Turnstile();
};

class Mutex {
 private:
    Turnstile *local;

 public:
    Mutex();
    Mutex(const Mutex&) = delete;

    void lock();    // NOLINT
    void unlock();  // NOLINT
};

#endif  // SRC_TURNSTILE_H_
