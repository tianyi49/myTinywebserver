#include "heap_timer.h"
#include "../http/http_conn.h"
#include <memory>
void heap_timer::percolate_down(int hole) {
  shared_ptr<util_timer> tem = m_heapVec[hole];
  int child = 0;
  for (; (hole * 2 + 1) <= (m_size - 1); hole = child) {
    child = 2 * hole + 1;
    if (child < m_size - 1 &&
        m_heapVec[child]->expire > m_heapVec[child + 1]->expire)
      ++child;
    if (tem->expire > m_heapVec[child]->expire) {
      m_heapVec[hole] = m_heapVec[child];
      timer2index[m_heapVec[hole]] = hole;
    } else
      break;
  }
  m_heapVec[hole] = tem;
  timer2index[tem] = hole;
}
void heap_timer::add_timer(shared_ptr<util_timer> timer) {
  if (!timer.get())
    return;
  int hole = m_size;
  int parent = 0;
  if (m_size < initHeapSize)
    m_heapVec[m_size] = timer;
  else
    m_heapVec.emplace_back(timer); // 放到末尾
  m_size++;
  auto tem = timer;
  for (; hole > 0; hole = parent) {
    parent = (hole - 1) / 2;
    if (m_heapVec[parent]->expire > tem->expire) {
      m_heapVec[hole] = m_heapVec[parent];
      timer2index[m_heapVec[hole]] = hole;
    }

    else
      break;
  }
  m_heapVec[hole] = tem;
  timer2index[tem] = hole;
}

void heap_timer::pop_timer() {
  if (m_heapVec.empty())
    return;
  if (m_heapVec[0]) {
    timer2index.erase(m_heapVec[0]);
    m_heapVec[0] = m_heapVec[--m_size];
    if (m_heapVec.size() > initHeapSize)
      m_heapVec.pop_back();
    else
      m_heapVec[m_size] = nullptr;
    percolate_down(0);
  }
}
void heap_timer::pop_timer(int index) {
  if (m_heapVec.empty())
    return;
  if (m_heapVec[index]) {
    timer2index.erase(m_heapVec[index]);
    m_heapVec[index] = m_heapVec[--m_size];
    if (m_heapVec.size() > initHeapSize)
      m_heapVec.pop_back();
    else
      m_heapVec[m_size] = nullptr;
    percolate_down(index);
  }
}
void heap_timer::del_timer1(shared_ptr<util_timer> timer) {
  if (!timer) {
    return;
  }
  pop_timer(timer2index[timer]);
}
void heap_timer::adjust_timer(shared_ptr<util_timer> timer) {
  if (!timer)
    return;
  percolate_down(timer2index[timer]);
}
void heap_timer::tick() {
  time_t cur = time(NULL);
  while (m_size > 0) {
    auto tmp = m_heapVec[0];
    if (!tmp)
      break;
    if (tmp->expire > cur)
      break;
    if (tmp->cb_func)
      cb_func(tmp->user_data);
    pop_timer();
  }
}

void Utils::init(int timeslot) { m_TIMESLOT = timeslot; }

// 信号处理函数
void Utils::sig_handler(int sig) {
  // 为保证函数的可重入性，保留原来的errno
  int save_errno = errno;
  int msg = sig;
  send(u_pipefd[1], (char *)&msg, 1, 0);
  errno = save_errno;
}

// 设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = handler;
  if (restart)
    sa.sa_flags |= SA_RESTART;//重启中断处的代码
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler() {
  m_heap_timer.tick();
  alarm(m_TIMESLOT);
}
void Utils::show_error(int connfd, const char *info) {
  send(connfd, info, sizeof(info), 0);
  close(connfd);
}
int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;
class Utils;
void cb_func(client_data *user_data) {
  epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd,
            0); // 删除监控的连接文件描述符
  assert(user_data);
  close(user_data->sockfd);
  http_conn::m_user_count--; // 结束这个连接总数量--
}