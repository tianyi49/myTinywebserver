#ifndef THREADPOOL_H
#define THREADPOOL_H
#include "../CGImysql/sql_connection_pool.h"
#include "../lock/lock.h"
#include <cstdio>
#include <exception>
#include <list>
#include <pthread.h>

template <typename T> class threadpool {
public:
  /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
  threadpool(int actor_model, connection_pool *connpool, int thread_num = 8,
             int max_request = 10000);
  ~threadpool();
  bool append(T *request, int stat);
  bool append_p(T *request);

private:
  /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
  static void *worker(void *arg);
  void run();

private:
  int m_thread_num;     // 线程池中的线程数
  int m_max_requests;   // 请求队列中允许的最大请求数
  pthread_t *m_threads; // 描述线程池的数组，其大小为m_thread_num
  std::list<T *> m_workqueue;  // 请求队列
  locker m_queuelocker;        // 保护请求队列的互斥锁
  sem m_queuestat;             // 是否有任务需要处理
  connection_pool *m_connPool; // 数据库连接池
  int m_actor_model;           // 模型切换
};
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connpool,
                          int thread_num, int max_request)
    : m_thread_num(thread_num), m_max_requests(max_request), m_threads(NULL),
      m_connPool(connpool), m_actor_model(actor_model) {
  if (max_request <= 0 || thread_num <= 0)
    throw std::exception();
  m_threads = new pthread_t[m_thread_num]; // 动态数组
  if (!m_threads)
    throw std::exception();
  for (int i = 0; i < m_thread_num; ++i) {
    if (pthread_create(&m_threads[i], NULL, worker, this) != 0) {
      delete[] m_threads;
      throw std::exception();
    }
    // 将线程进行分离后，不用单独对工作线程进行回收
    if (pthread_detach(m_threads[i])) {
      delete[] m_threads;
      throw std::exception();
    }
  }
}
template <typename T> threadpool<T>::~threadpool() { delete[] m_threads; }
template <typename T> bool threadpool<T>::append(T *request, int state) {
  m_queuelocker.lock();
  if (m_workqueue.size() >= m_max_requests) {
    m_queuelocker.unlock();
    return false;
  }
  request->m_state = state;
  m_workqueue.push_back(request);
  m_queuelocker.unlock();
  m_queuestat.post();
  return true;
}
template <typename T> bool threadpool<T>::append_p(T *request) {
  m_queuelocker.lock();
  if (m_workqueue.size() >= m_max_requests) {
    m_queuelocker.unlock();
    return false;
  }
  m_workqueue.push_back(request);
  m_queuelocker.unlock();
  m_queuestat.post();
  return true;
}
template <typename T> void *threadpool<T>::worker(void *arg) {
  threadpool *pool = (threadpool *)arg;
  pool->run();
  return pool;
}
template <typename T> void threadpool<T>::run() {

  while (true) {
    m_queuestat.wait();
    m_queuelocker.lock();
    if (m_workqueue.empty()) {
      m_queuelocker.unlock();
      continue;
    }
    T *request = m_workqueue.front();
    m_workqueue.pop_front();
    m_queuelocker.unlock();
    if (!request)
      continue;
    if (1 == m_actor_model) // reactor模式
    {
      if (0 == request->m_state) // 读为0
      {
        if (request->read_once()) {
          request->improv = 1; // 读写数据完成，进入逻辑处理
          connectionRAII mysqlcon(&request->mysql, m_connPool);
          request->process();
        } else {
          request->close_conn();
          request->improv = 1;
          request->timer_flag = 1;
        }
      } else {
        if (request->write()) {
          request->improv = 1;
        } else {
          request->close_conn();
          request->improv = 1;
          request->timer_flag = 1;
        }
      }
    }
    // 模拟proactor
    else {
      connectionRAII mysqlcon(&request->mysql, m_connPool);
      request->process();
    }
  }
}

#endif