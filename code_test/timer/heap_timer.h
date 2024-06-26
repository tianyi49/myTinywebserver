#ifndef HEAP_TIMER
#define HEAP_TIMER
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <unordered_map>
#include <vector>
using namespace std;
class util_timer;
struct client_data {
  sockaddr_in address;
  int sockfd;
  shared_ptr<util_timer> timer;
};

class util_timer {
public:
  time_t expire;
  void (*cb_func)(client_data *);
  client_data *user_data;
};
class heap_timer {
private:
  vector<shared_ptr<util_timer>> m_heapVec; // 堆数组
  int m_size = 0; // 多线程情况会不会造成Bug?是会的，
                  // 但这里只在主线程使用,必须是int否则隐式的类型转换导致越界
public:
  heap_timer() = default;
  ~heap_timer() = default;

public:
  size_t get_size() { return m_heapVec.size(); }
  size_t get_msize() { return m_size; }

  void add_timer(shared_ptr<util_timer> timer);
  // 节省删除开销
  void del_timer(shared_ptr<util_timer> timer) {
    if (!timer) {
      return;
    }
    // lazy delelte
    timer->cb_func = NULL;
  }
  shared_ptr<util_timer> top() const {
    if (m_heapVec.empty())
      return nullptr;
    return m_heapVec[0];
  }
  void pop_timer();
  void adjust_timer(shared_ptr<util_timer> timer); // 只会往后延长时间
  void tick();

private:
  void percolate_down(int hole);
};

class Utils {
public:
  Utils() {}
  ~Utils() {}

  void init(int timeslot);
  // 信号处理函数
  static void sig_handler(int sig);

  // 设置信号函数
  void addsig(int sig, void(handler)(int), bool restart = true);

  // 定时处理任务，重新定时以不断触发SIGALRM信号
  void timer_handler();

  void show_error(int connfd, const char *info); // 接受连接之后超出最大连接树木

public:
  static int *u_pipefd;
  heap_timer m_heap_timer;
  static int u_epollfd;
  int m_TIMESLOT;
};
void cb_func(client_data *user_data);
#endif