#include "webserver.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <ctime>
#include <memory>
#include <unistd.h>
int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;
  if (TRIGMode) {
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  } else {
    event.events = EPOLLIN | EPOLLRDHUP;
  }
  if (one_shot)
    event.events |= EPOLLONESHOT;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

WebServer::WebServer() {
  // http_conn类对象
  users = new http_conn[MAX_FD];
  // root文件夹路径
  char server_path[200];
  getcwd(server_path, 200);
  char root[6] = "/root";
  m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
  strcpy(m_root, server_path);
  strcat(m_root + strlen(server_path), root);
  // 定时器
  users_timer = new client_data[MAX_FD];
}
WebServer::~WebServer() {
  // 关闭文件描述符
  close(m_listenfd);
  close(m_epollfd);
  close(m_pipefd[0]);
  close(m_pipefd[1]);
  delete[] users;
  delete[] users_timer;
  delete m_pool;
}
void WebServer::timer(int connfd, struct sockaddr_in client_address) {
  users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode,
                     m_close_log, m_user, m_passWord,
                     m_databaseName); // 复用初始化
  // 初始化client_data数据
  // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
  users_timer[connfd].address = client_address;
  users_timer[connfd].sockfd = connfd;
  auto timer = make_shared<util_timer>();
  timer->user_data = &users_timer[connfd];
  timer->cb_func = cb_func;
  time_t cur = time(NULL);
  timer->expire = cur + 3 * TIMESLOT;
  users_timer[connfd].timer = timer;
  utils.m_heap_timer.add_timer(timer);
}
// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(shared_ptr<util_timer> timer) {
  time_t cur = time(NULL);
  timer->expire = cur + 3 * TIMESLOT;
  utils.m_heap_timer.adjust_timer(timer);
  LOG(LoggerLevel::INFO, "%s", "adjust timer once");
}

void WebServer::deal_timer(
    shared_ptr<util_timer> timer,
    int sockfd) { // 释放了http连接的所有资源，定时器，epoll注册的fd,connfd
  timer->cb_func(&users_timer[sockfd]);
  if (timer)
    utils.m_heap_timer.del_timer(timer);
  LOG(LoggerLevel::INFO, "close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata() {
  struct sockaddr_in client_address;
  socklen_t client_len = sizeof(client_address);
  if (0 == m_LISTENTrigmode) // LT
  {
    int connfd = accept(m_listenfd, (sockaddr *)&client_address, &client_len);
    if (connfd < 0) {
      LOG(LoggerLevel::ERROR, "%s:errno is %d", "accept error", errno);
      return false;
    }
    if (http_conn::m_user_count >= MAX_FD) {
      utils.show_error(connfd, "Internal server busy");
      LOG(LoggerLevel::ERROR, "%s", "Internal server busy");
      return false;
    }
    timer(connfd, client_address);
  } else // et
  {
    while (true) {
      int connfd = accept(m_listenfd, (sockaddr *)&client_address, &client_len);
      if (connfd < 0) {
        LOG(LoggerLevel::ERROR, "%s:errno is:%d", "accept error", errno);
        return false;
      }
      if (http_conn::m_user_count >= MAX_FD) {
        utils.show_error(connfd, "Internal server busy");
        LOG(LoggerLevel::ERROR, "%s", "Internal server busy");
        return false;
      }
      timer(connfd, client_address);
    }
  }
  return true;
}
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server) {
  int ret = 0;
  char signal[1024];
  ret = recv(m_pipefd[0], signal, sizeof(signal), 0);
  if (ret <= 0) {
    return false;
  }
  for (int i = 0; i < ret; ++i)
    switch (signal[i]) {
    case SIGTERM: {
      stop_server = true;
      break;
    }
    case SIGALRM: {
      timeout = true;
      break;
    }
    }
  return true;
}
void WebServer::dealwithread(int sockfd) {
  shared_ptr<util_timer> timer = users_timer[sockfd].timer;
  // reactor
  if (1 == m_actormodel) {
    m_pool->append(users + sockfd, 0);
  } else { // proactor
    int readErrno = 0;
    if (users[sockfd].read_once(&readErrno)) {
      LOG(LoggerLevel::INFO, "deal with the client(%s)",
          inet_ntoa(users[sockfd].get_address()->sin_addr))
      // 若监测到读事件，将该事件放入请求队列
      m_pool->append_p(users + sockfd);

      if (timer) {
        adjust_timer(timer);
      }
    } else {
      deal_timer(timer, sockfd);
      printf("dealwithread:deal_timer()"); // 不应该出现
    }
  }
}
void WebServer::dealwithwrite(int sockfd) {
  shared_ptr<util_timer> timer = users_timer[sockfd].timer;
  if (1 == m_actormodel) {
    m_pool->append(users + sockfd, 1);
  } else { // proactor
    int writeErrno = 0;
    if (users[sockfd].write(&writeErrno)) {
      LOG(LoggerLevel::INFO, "send data to the client(%s)",
          inet_ntoa(users[sockfd].get_address()->sin_addr));
      if (timer) {
        adjust_timer(timer);
      }
    } else {
      deal_timer(timer, sockfd);
      printf("dealwithwrite:deal_timer()"); // 不应该出现
    }
  }
}

void WebServer::init(int port, string user, string passWord,
                     string databaseName, int log_write, int opt_linger,
                     int trigmode, int sql_num, int thread_num, int close_log,
                     int actor_model) {
  m_port = port;
  m_user = user;
  m_passWord = passWord;
  m_databaseName = databaseName;
  m_log_write = log_write;
  m_OPT_LINGER = opt_linger;
  m_TRIGMode = trigmode;
  m_sql_num = sql_num;
  m_thread_num = thread_num;
  m_close_log = close_log;
  m_actormodel = actor_model;
}
void WebServer::trig_mode() {
  // LT + LT
  if (0 == m_TRIGMode) {
    m_LISTENTrigmode = 0;
    m_CONNTrigmode = 0;
  }
  // LT + ET
  else if (1 == m_TRIGMode) {
    m_LISTENTrigmode = 0;
    m_CONNTrigmode = 1;
  }
  // ET + LT
  else if (2 == m_TRIGMode) {
    m_LISTENTrigmode = 1;
    m_CONNTrigmode = 0;
  }
  // ET + ET
  else if (3 == m_TRIGMode) {
    m_LISTENTrigmode = 1;
    m_CONNTrigmode = 1;
  }
}
void WebServer::log_write() {

  if (0 == m_close_log) {
    Logger::GetInstance()->Init("./log_record", LoggerLevel::INFO, m_close_log,
                                800000);
  }
}
void WebServer::sql_pool() {
  // 初始化数据库连接池
  m_connPool = connection_pool::GetInstance(); // 单例模式
  m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306,
                   m_sql_num, m_close_log);
  // 初始化数据库读取表
  users->initmysql_result(m_connPool); // 加载用户名和密码到map数据结构中
}
void WebServer::thread_pool() {
  // 线程池
  m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}
void WebServer::eventListen() {
  m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(m_listenfd >= 0);
  struct sockaddr_in address;
  // 设置优雅的关闭
  if (m_OPT_LINGER) {
    struct linger tem = {1, 1};
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tem, sizeof(tem));
  } else {
    struct linger tem = {0, 1};
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tem, sizeof(tem));
  }
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(m_port); // 字节序转换
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret;
  int flag = 1;
  setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
  ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
  assert(ret != -1);
  ret = listen(m_listenfd, 1024);
  assert(ret != -1);
  utils.init(TIMESLOT);

  // epoll创建内核事件表
  m_epollfd = epoll_create(5);
  assert(m_epollfd != -1);
  addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
  http_conn::m_epollfd = m_epollfd;

  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
  assert(ret != -1);
  setnonblocking(m_pipefd[1]);
  addfd(m_epollfd, m_pipefd[0], false, 0);

  utils.addsig(SIGPIPE, SIG_IGN);
  utils.addsig(SIGALRM, utils.sig_handler, false);
  utils.addsig(SIGTERM, utils.sig_handler, false);

  alarm(TIMESLOT);
  // 工具类,信号和描述符基础操作
  Utils::u_pipefd = m_pipefd;
  Utils::u_epollfd = m_epollfd;
};

void WebServer::eventLoop() {
  bool timeout = false;
  bool stop_server = false;
  while (!stop_server) {
    int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    if (number < 0 && errno != EINTR) { // 之前写为Eagin导致每次断点都·退出程序
      LOG(LoggerLevel::ERROR, "%s", "epoll failure");
      break;
    }
    for (int i = 0; i < number; i++) {
      int sockfd = events[i].data.fd;
      if (sockfd == m_listenfd) // 创建连接
      {
        bool flag = dealclinetdata();
        if (flag == false)
          continue;
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // 服务器端关闭连接，移除对应的定时器
        shared_ptr<util_timer> timer = users_timer[sockfd].timer;
        deal_timer(timer, sockfd);
      } else if (sockfd == m_pipefd[0] && (events[i].events & EPOLLIN)) {
        bool flag = dealwithsignal(timeout, stop_server); // 处理信号
        if (false == flag)
          LOG(LoggerLevel::ERROR, "%s", "dealclientdata failure");
      } else if (events[i].events & EPOLLIN) {
        dealwithread(sockfd); // 处理客户连接上接收到的数据
      } else if (events[i].events & EPOLLOUT) {
        dealwithwrite(sockfd);
      }
    }
    // if (utils.m_heap_timer.get_size() || utils.m_heap_timer.get_msize())
    //   printf("heap_timer:%d ,m_size:%d \n", utils.m_heap_timer.get_size(),
    //          utils.m_heap_timer.get_msize());
    if (timeout) // 处理定时事件
    {
      utils.timer_handler(); // 定时器检测连接过期情况，重新发送定时信号
      LOG(LoggerLevel::INFO, "%s", "timer tick");
      timeout = false;
    }
  }
}
