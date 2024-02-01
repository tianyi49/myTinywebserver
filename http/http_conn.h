#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include "../CGImysql/sql_connection_pool.h"
#include "../buffer/buffer.h"
#include "../lock/lock.h"
#include "../log/log.h"
#include "../timer/heap_timer.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
class http_conn {
public:
  http_conn() : m_read_buf(READ_BUFFER_SIZE), m_write_buf(WRITE_BUFFER_SIZE) {}
  static const int FILENAME_LEN = 200;
  static const int READ_BUFFER_SIZE = 2048;
  static const int WRITE_BUFFER_SIZE = 1024;
  enum METHOD {
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATH
  };
  enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  enum HTTP_CODE {
    NO_REQUEST,  // 请求不完整，需要继续读取请求报文数据
    GET_REQUEST, //- GET_REQUEST获得了完整的HTTP请求
    BAD_REQUEST, //- HTTP请求报文有语法错误
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR, // 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
    CLOSED_CONNECTION
  };
  enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
  void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user,
            string passwd, string sqlname);
  void close_conn(bool real_close = true);
  void process();
  bool read_once(int *saveErrno);
  bool write(int *saveErrno);
  sockaddr_in *get_address() { return &m_address; }
  void initmysql_result(connection_pool *connPool);

private:
  void init();
  HTTP_CODE process_read();
  bool process_write(HTTP_CODE ret);
  HTTP_CODE parse_request_line(char *text);
  HTTP_CODE parse_headers(char *text);
  HTTP_CODE parse_content(char *text);
  HTTP_CODE do_request();
  char *get_line() { return m_read_buf.BeginPtr() + m_start_line; };
  LINE_STATUS parse_line();
  void unmap();
  void add_content(const char *content);
  void add_status_line(int status, const char *title);
  void add_headers(int content_length);
  void add_content_type();

public:
  static int m_epollfd;
  static int m_user_count;
  MYSQL *mysql;
  int m_state; // 读为0, 写为1

private:
  int m_sockfd;
  sockaddr_in m_address;
  ReadBuffer m_read_buf;
  long m_read_idx;
  long m_checked_idx;
  int m_start_line;
  WriteBuffer m_write_buf;

  int m_write_idx; // 指示buffer中的长度
  CHECK_STATE m_check_state;
  METHOD m_method;
  char m_real_file[FILENAME_LEN];
  char *m_url;
  char *m_version;
  char *m_host;
  long m_content_length;
  bool m_linger;
  char *m_file_address;
  struct stat m_file_stat;
  int cgi;        // 是否启用的POST
  char *m_string; // 存储请求头数据
  int bytes_to_send;
  int bytes_have_send;
  char *doc_root; // root文件目录路径

  static map<string, string> m_users;
  static locker m_lock;
  int m_TRIGMode;
  int m_close_log;

  char sql_user[100];
  char sql_passwd[100];
  char sql_name[100];
};
extern int setnonblocking(int fd); // 对文件描述符设置非阻塞
extern void
addfd(int epollfd, int fd, bool one_shot,
      int TRIGMode); // 将内核事件表注册读事件，选择开启EPOLLONESHOT,ET模式，
#endif