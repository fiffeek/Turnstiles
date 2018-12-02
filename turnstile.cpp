#include <queue>
#include <map>
#include <iostream>
#include <thread>
#include <condition_variable>
#include "turnstile.h"

namespace {
    class TurnstilesFactory {
     private:
        std::queue<Turnstile*> turnstilesPool;
        size_t turnstiles;

        void addTurnstiles() {
            for (size_t i = 0; i < turnstiles; ++i) {
                turnstilesPool.push(new Turnstile());
            }

            turnstiles *= 2;
        }

        Turnstile* frontAndPop() {
            Turnstile *toRet = turnstilesPool.front();
            turnstilesPool.pop();
            return toRet;
        }

        bool empty() {
            return turnstilesPool.empty();
        }

     public:
        TurnstilesFactory()
                :   turnstiles(4) {
            for (size_t i = 0; i < turnstiles; ++i) {
                turnstilesPool.push(new Turnstile());
            }
        }

        ~TurnstilesFactory() {
            while (!turnstilesPool.empty()) {
                Turnstile *toRem = turnstilesPool.front();
                turnstilesPool.pop();
                delete toRem;
            }
        }

        inline std::mutex& getLocalMutex(Mutex *ptr) {
            static const size_t mutexes_count = 256;
            static std::mutex mutexes[mutexes_count];

            auto number = reinterpret_cast<uintptr_t>(ptr);
            number = (number >> 3) % mutexes_count;
            return mutexes[number];
        }

        void returnToPool(Turnstile *toRet) {
            turnstilesPool.push(toRet);

            if (4 * turnstilesPool.size() >= 3 * turnstiles) {
                size_t deletions = turnstiles / 2;
                turnstiles -= deletions;

                while (deletions > 0) {
                    Turnstile *toRem = turnstilesPool.front();
                    turnstilesPool.pop();
                    delete toRem;
                    --deletions;
                }
            }
        }

        Turnstile* getFromPool() {
            if (empty()) {
                addTurnstiles();
            }

            return frontAndPop();
        }
    };

    std::mutex shield;

    char none_turnstile = 'a';

    Turnstile *const dummy = reinterpret_cast<Turnstile*>(&none_turnstile);
}

TurnstilesFactory& turnstiles() {
    static TurnstilesFactory temp;
    return temp;
}

Turnstile::Turnstile()
        : waiting(0), goThrough(false) {
    locker = new std::mutex;
}

Turnstile::~Turnstile() {
    delete locker;
}

Mutex::Mutex() {
    local = nullptr;
}

void Mutex::lock() {
    turnstiles().getLocalMutex(this).lock();

    if (local == nullptr) {
        this->local = dummy;
    } else  {
        if (local == dummy) {
            shield.lock();
            this->local = turnstiles().getFromPool();
            shield.unlock();
        }

        this->local->waiting++;
        turnstiles().getLocalMutex(this).unlock();
        std::unique_lock<std::mutex> lk(*(this->local->locker));

        this->local->cv.wait(lk, [=] { return local->goThrough; });
        turnstiles().getLocalMutex(this).lock();
        this->local->goThrough = false;
        this->local->waiting--;
    }

    turnstiles().getLocalMutex(this).unlock();
}

void Mutex::unlock() {
    turnstiles().getLocalMutex(this).lock();
    assert(local != nullptr);

    if (local == dummy) {
        this->local = nullptr;
        turnstiles().getLocalMutex(this).unlock();
    } else {
        if (local->waiting == 0) {
            local->goThrough = false;

            shield.lock();
            turnstiles().returnToPool(local);
            shield.unlock();

            local = nullptr;
            turnstiles().getLocalMutex(this).unlock();
        } else {
            std::unique_lock<std::mutex> lk(*(this->local->locker));
            this->local->goThrough = true;
            this->local->cv.notify_one();
            turnstiles().getLocalMutex(this).unlock();
        }
    }
}

//void Mutex::lock() {
//    shield.lock();
//
//    if (local == nullptr) {
//        this->local = turnstiles().getFromPool();
//        this->local->locker->lock();
//    } else {
//        this->local->waiting++;
//        shield.unlock();
//        this->local->locker->lock();  // sharing mutex
//        this->local->waiting--;
//    }
//
//    shield.unlock();
//}
//
//void Mutex::unlock() {
//    shield.lock();
//
//    if (local->waiting == 0) {
//        this->local->locker->unlock();
//        turnstiles().returnToPool(local);
//        local = nullptr;
//
//        shield.unlock();
//    } else {
//        this->local->locker->unlock();  // mutex forwarding
//    }
//}
