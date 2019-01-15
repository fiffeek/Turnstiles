#include "turnstile.h"
#include <condition_variable>
#include <iostream>
#include <map>
#include <queue>
#include <thread>

namespace {

class TurnstilesFactory {
 private:
  std::queue<Turnstile *> turnstilesPool;
  size_t turnstiles;
  std::mutex mutexes[257];

  void addTurnstiles() {
    for (size_t i = 0; i < turnstiles; ++i) {
      turnstilesPool.push(new Turnstile());
    }

    turnstiles *= 2;
  }

  Turnstile *frontAndPop() {
    Turnstile *toRet = turnstilesPool.front();
    turnstilesPool.pop();
    return toRet;
  }

  bool empty() { return turnstilesPool.empty(); }

 public:
  TurnstilesFactory() : turnstiles(4) {
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

  inline std::mutex &getLocalMutex(Mutex *ptr) {
    auto number = reinterpret_cast<uintptr_t>(ptr);
    number = (number >> 3) % 257;
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

  Turnstile *getFromPool() {
    if (empty()) {
      addTurnstiles();
    }

    return frontAndPop();
  }
};

std::mutex shield;

char noneTurnstile = 'a';

Turnstile *const dummy = reinterpret_cast<Turnstile *>(&noneTurnstile);
}  // namespace

TurnstilesFactory &turnstiles() {
  static TurnstilesFactory temp;
  return temp;
}

Turnstile::Turnstile() : waiting(0), goThrough(false) {
  locker = new std::mutex;
}

Turnstile::~Turnstile() { delete locker; }

Mutex::Mutex() { local = nullptr; }

void Mutex::lock() {
  turnstiles().getLocalMutex(this).lock();

  if (local == nullptr) {
    this->local = dummy;
  } else {
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

    if (this->local->waiting == 0) {
      shield.lock();
      turnstiles().returnToPool(local);
      shield.unlock();

      local = dummy;
    }
  }

  turnstiles().getLocalMutex(this).unlock();
}

void Mutex::unlock() {
  turnstiles().getLocalMutex(this).lock();

  if (local == dummy) {
    this->local = nullptr;
    turnstiles().getLocalMutex(this).unlock();
  } else {
    this->local->locker->lock();

    this->local->goThrough = true;
    this->local->cv.notify_one();

    this->local->locker->unlock();
    turnstiles().getLocalMutex(this).unlock();
  }
}
