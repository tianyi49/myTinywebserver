#include "lst_timer.h"
#include "../http/http_conn.h"
sort_timer_lst::sort_timer_lst() {
  head = NULL;
  tail = NULL;
}
sort_timer_lst::~sort_timer_lst() {
  util_timer *tmp = head;
  while (tmp) {
    head = tmp->next;
    delete tmp;
    tmp = head;
  }
}

void sort_timer_lst::add_timer(util_timer *timer) {
  if (!timer) {
    return;
  }
  if (!head) {
    head = tail = timer;
    return;
  }
  if (timer->expire < head->expire) {
    timer->next = head;
    head->prev = timer;
    head = timer;
    return;
  }
  add_timer(timer, head);
}
void sort_timer_lst::adjust_timer(util_timer *timer) {
  if (!timer) {
    return;
  }
  util_timer *tmp = timer->next;
  if (!tmp || (timer->expire < tmp->expire)) {
    return;
  }
  if (timer == head) {
    head = head->next;
    head->prev = NULL;
    timer->next = NULL;
    add_timer(timer, head);
  } else {
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    add_timer(timer, timer->next);
  }
}
void sort_timer_lst::del_timer(util_timer *timer) {
  if (!timer) {
    return;
  }
  if ((timer == head) && (timer == tail)) {
    delete timer;
    head = NULL;
    tail = NULL;
    return;
  }
  if (timer == head) {
    head = head->next;
    head->prev = NULL;
    delete timer;
    return;
  }
  if (timer == tail) {
    tail = tail->prev;
    tail->next = NULL;
    delete timer;
    return;
  }
  timer->prev->next = timer->next;
  timer->next->prev = timer->prev;
  delete timer;
}
void sort_timer_lst::tick() {
  if (!head) {
    return;
  }

  time_t cur = time(NULL);
  util_timer *tmp = head;
  while (tmp) {
    if (cur < tmp->expire) {
      break;
    }
    tmp->cb_func(tmp->user_data);
    head = tmp->next;
    if (head) {
      head->prev = NULL;
    }
    delete tmp;
    tmp = head;
  }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
  util_timer *prev = lst_head;
  util_timer *tmp = prev->next;
  while (tmp) {
    if (timer->expire < tmp->expire) {
      prev->next = timer;
      timer->next = tmp;
      tmp->prev = timer;
      timer->prev = prev;
      break;
    }
    prev = tmp;
    tmp = tmp->next;
  }
  if (!tmp) {
    prev->next = timer;
    timer->prev = prev;
    timer->next = NULL;
    tail = timer;
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
    sa.sa_flags |= SA_RESTART;
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler() {
  m_timer_lst.tick();
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