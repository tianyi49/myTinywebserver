#ifndef LOCK_H
#define LOCK_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
class sem {
public:
  sem() {
    if (sem_init(&m_sem, 0, 0) != 0)
      throw std::exception();
  }
  sem(int value) {
    if (sem_init(&m_sem, 0, value) != 0)
      throw std::exception();
  }
  ~sem() { sem_destroy(&m_sem); }
  bool wait() {
    return sem_wait(&m_sem) == 0; }
  bool post() { return sem_post(&m_sem) == 0; }

private:
  sem_t m_sem;
};
class locker {
public:
  locker() {
    if (pthread_mutex_init(&m_lock, NULL) != 0)
      throw std::exception();
  }
  ~locker() { pthread_mutex_destroy(&m_lock); }
  bool lock() { return pthread_mutex_lock(&m_lock) == 0; }
  bool unlock() { return pthread_mutex_unlock(&m_lock) == 0; }
  pthread_mutex_t *get() { return &m_lock; }

private:
  pthread_mutex_t m_lock;
};
class cond {
public:
  cond() {
    if (pthread_cond_init(&m_cond, NULL) != 0) {
      throw std::exception();
    }
  }
  ~cond() { pthread_cond_destroy(&m_cond); }
  bool wait(pthread_mutex_t *mutex) {
    return pthread_cond_wait(&m_cond, mutex) == 0;
  }
  bool timewait(pthread_mutex_t *mutex, struct timespec t) {
    return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
  }
  bool signal() { return pthread_cond_signal(&m_cond) == 0; }
  bool broadcast() {
    pthread_cond_broadcast(&m_cond);
    return true;
  }

private:
  pthread_cond_t m_cond;
};

#endif