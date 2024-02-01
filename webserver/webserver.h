#ifndef WEBSERVER_H
#define WEBSERVER_H
#include "../http/http_conn.h"
#include "../threadpool/threadpool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/unistd.h>

using namespace std;
const int MAX_FD = 65535; // 预先分配http连接数组，大概200m
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 1000; // 最小超时单位

int setnonblocking(int fd); // 对文件描述符设置非阻塞
void addfd(
    int epollfd, int fd, bool one_shot,
    int TRIGMode); // 将内核事件表注册读事件，选择开启EPOLLONESHOT,ET模式，
class WebServer {
public:
  WebServer();
  ~WebServer();
  WebServer(const WebServer &) = delete;
  WebServer &operator=(const WebServer &) = delete;

  void init(int port, string user, string passWord, string databaseName,
            int log_write, int opt_linger, int trigmode, int sql_num,
            int thread_num, int close_log, int actor_model);
  void thread_pool();
  void sql_pool();
  void log_write();

  void timer(int connfd, struct sockaddr_in client_address);
  void adjust_timer(shared_ptr<util_timer> timer);
  void deal_timer(shared_ptr<util_timer> timer, int sockfd);
  bool dealclinetdata();
  bool dealwithsignal(bool &timeout, bool &stop_server);
  void dealwithread(int sockfd);
  void dealwithwrite(int sockfd);

  void trig_mode();
  void eventListen();
  void eventLoop(); // 事件循环

public:
  // 基础
  int m_port;
  char *m_root;
  int m_log_write;
  int m_close_log;
  int m_actormodel;

  int m_pipefd[2];
  int m_epollfd;
  http_conn *users;

  // 数据库相关
  connection_pool *m_connPool;
  string m_user;         // 登陆数据库用户名
  string m_passWord;     // 登陆数据库密码
  string m_databaseName; // 使用数据库名
  int m_sql_num;

  // 线程池相关
  threadpool<http_conn> *m_pool;
  int m_thread_num;

  // epoll_event相关
  epoll_event events[MAX_EVENT_NUMBER];

  int m_listenfd;
  int m_OPT_LINGER;
  int m_TRIGMode;
  int m_LISTENTrigmode; // 0：LT
  int m_CONNTrigmode;   // 0：LT

  // 定时器相关
  client_data *users_timer;
  Utils utils;
};

#endif