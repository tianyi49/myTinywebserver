#include "http_conn.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mysql/mysql.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form =
    "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form =
    "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form =
    "There was an unusual problem serving the request file.\n";

locker http_conn::m_lock;
map<string, string> http_conn::m_users;
void http_conn::initmysql_result(connection_pool *connPool) {
  MYSQL *mysql = NULL;
  connectionRAII mysqlcon(&mysql, connPool);

  if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
    LOG(LoggerLevel::ERROR, "SELECT error:%s\n", mysql_error(mysql))
  }
  // 从表中检索完整的结果集
  MYSQL_RES *result = mysql_store_result(mysql);

  // 返回结果集中的列数，没用到
  int num_fields = mysql_num_fields(result);
  // 返回所有字段结构的数组，没用到
  MYSQL_FIELD *fields = mysql_fetch_fields(result);
  while (MYSQL_ROW row = mysql_fetch_row(result)) {
    m_users[row[0]] = row[1];
  }
  mysql_free_result(result);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
  close(fd);
}
// 将事件重置为EPOLLONESHOT,TRIGMode为连接TRIGMode
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
  epoll_event event;
  event.data.fd = fd;
  if (TRIGMode == 1)
    event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
  else
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
  if (real_close && (m_sockfd != -1)) {
    // printf("close %d\n", m_sockfd);
    removefd(m_epollfd, m_sockfd);
    m_sockfd = -1;
    m_user_count--;
  }
}
// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root,
                     int TRIGMode, int close_log, string user, string passwd,
                     string sqlname) {
  m_sockfd = sockfd;
  m_address = addr;
  addfd(m_epollfd, m_sockfd, true, m_TRIGMode);
  m_user_count++;
  // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
  doc_root = root;
  m_TRIGMode = TRIGMode;
  m_close_log = close_log;

  strcpy(sql_user, user.c_str());
  strcpy(sql_passwd, passwd.c_str());
  strcpy(sql_name, sqlname.c_str());

  init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init() {
  mysql = NULL;
  bytes_to_send = 0;
  bytes_have_send = 0;
  m_check_state = CHECK_STATE_REQUESTLINE;
  m_linger = false;
  m_method = GET;
  m_url = 0;
  m_version = 0;
  m_content_length = 0;
  m_host = 0;
  m_start_line = 0;
  m_checked_idx = 0;
  m_read_idx = 0;
  m_write_idx = 0;
  cgi = 0;
  m_state = 0;
  m_read_buf.RetrieveAll();
  m_write_buf.RetrieveAll();
}
// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line() {
  char temp;
  for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
    temp = m_read_buf[m_checked_idx];
    if (temp == '\r') {
      if (m_checked_idx + 1 < m_read_idx) {
        if (m_read_buf[m_checked_idx + 1] == '\n') {
          m_read_buf[m_checked_idx++] = '\0';
          m_read_buf[m_checked_idx++] = '\0';
          return LINE_OK;
        } else {
          return LINE_BAD;
        }
      } else
        return LINE_BAD;
    } else if (temp == '\n') {
      if (m_checked_idx == 0)
        return LINE_BAD;
      else if (m_read_buf[m_checked_idx - 1] == 'r') {
        m_read_buf[m_checked_idx - 1] = '\0';
        m_read_buf[m_checked_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  return LINE_OPEN;
}
// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(int *saveErrno) {
  int bytes_read = 0;
  // LT读取数据
  if (0 == m_TRIGMode) {
    bytes_read = m_read_buf.TransFd(m_sockfd, saveErrno);
    if (bytes_read <= 0)
      return false;
    m_read_idx += bytes_read;
    return true;
  } else {
    while (1) {
      bytes_read = m_read_buf.TransFd(m_sockfd, saveErrno);
      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        return false;
      } else if (bytes_read == 0)
        return false;
      m_read_idx += bytes_read;
    }
    return true;
  }
}
// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
  m_url = strpbrk(text, " \t");
  if (!m_url) {
    return BAD_REQUEST;
  }
  *m_url++ = '\0';
  // 解析方法
  char *method = text;
  if (strcasecmp(method, "GET") == 0) {
    m_method = GET;
  } else if (strcasecmp(method, "POST") == 0) {
    m_method = POST;
    cgi = 1;
  } else
    return BAD_REQUEST;

  m_url += strspn(m_url, " \t");
  // 解析协议版本
  m_version = strpbrk(m_url, " \t");
  if (!m_version)
    return BAD_REQUEST;
  *m_version++ = '\0';
  m_version += strspn(m_version, " \t");
  if (strcasecmp(m_version, "HTTP/1.1") != 0)
    return BAD_REQUEST;
  // 解析连接
  if (strncasecmp(m_url, "http//:", 7) == 0) {
    m_url += 7;
    m_url = strchr(m_url, '/');
  } else if (strncasecmp(m_url, "https//:", 8) == 0) {
    m_url += 8;
    m_url = strchr(m_url, '/');
  }
  if (!m_url || m_url[0] != '/')
    return BAD_REQUEST;
  // 当url为/时，显示判断界面
  if (strlen(m_url) == 1) {
    strcat(m_url, "judge.html");
  }
  m_check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}
// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
  if (text[0] == '\0') {
    if (m_content_length != 0) // 继续解析,m_checked_idx指向content开头
    {
      m_check_state = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    return GET_REQUEST;
  } else if (strncasecmp(text, "HOST:", 5) == 0) {
    text += 5;
    text += strspn(text, " \t");
    m_host = text;
  } else if (strncasecmp(text, "Connection:", 11) == 0) {
    text += 11;
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0) {
      m_linger = true;
    }
  } else if (strncasecmp(text, "Content-length:", 15) == 0) {
    text += 15;
    text += strspn(text, " \t");
    m_content_length = atol(text);
  } else {
    LOG(LoggerLevel::INFO, "oop!unknow header: %s", text);
  }
  return NO_REQUEST;
}
// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
  if (m_read_idx >= m_checked_idx + m_content_length) {
    text[m_content_length] = '\0';
    // POST请求中最后为输入的用户名和密码
    m_string = text;
    return GET_REQUEST;
  }
  return NO_REQUEST; // 如果没有，继续读入
}
http_conn::HTTP_CODE http_conn::process_read() // 主状态机逻辑
{
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char *text = 0;

  while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
         ((line_status = parse_line()) == LINE_OK)) {
    text = get_line();
    m_start_line = m_checked_idx;
    switch (m_check_state) {
    case CHECK_STATE_REQUESTLINE: {
      ret = parse_request_line(text);
      if (ret == BAD_REQUEST) {
        return BAD_REQUEST;
        break;
      }
    }
    case CHECK_STATE_HEADER: {
      ret = parse_headers(text);
      if (ret == BAD_REQUEST)
        return BAD_REQUEST;
      else if (ret == GET_REQUEST) {
        return do_request();
      }
      break;
    }
    case CHECK_STATE_CONTENT: {
      ret = parse_content(text);
      if (ret == GET_REQUEST)
        return do_request();
      line_status = LINE_OPEN;
      break;
    }
    default:
      return INTERNAL_ERROR;
    }
  }
  return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::do_request() {
  strcpy(m_real_file, doc_root);
  int len = strlen(doc_root);
  const char *p = strchr(m_url, '/');
  // 处理cgi
  if (cgi == 1 && ((*(p + 1) == '2' || *(p + 1) == '3'))) {
    char flag = m_url[1];

    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/");
    strcat(m_url_real, m_url + 2);
    strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
    free(m_url_real);

    // 将用户名和密码提取出来
    // user=123&passwd=123
    char name[100], password[100];
    int i;
    for (i = 5; m_string[i] != '&'; i++) {
      name[i - 5] = m_string[i];
    }
    name[i - 5] = '\0';
    int j = 0;
    for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
      password[j] = m_string[i];
    password[j] = '\0';
    if (*(p + 1) == '3') {
      // 如果是注册，先检测数据库中是否有重名的
      // 没有重名的，进行增加数据
      char *sql_insert = (char *)malloc(sizeof(char) * 200);
      strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
      strcat(sql_insert, "'");
      strcat(sql_insert, name);
      strcat(sql_insert, "', '");
      strcat(sql_insert, password);
      strcat(sql_insert, "')");

      if (m_users.find(name) == m_users.end()) {
        m_lock.lock();
        int res = mysql_query(mysql, sql_insert);
        m_users[name] = password;
        if (!res)
          strcpy(m_url, "/log.html");
        else
          strcpy(m_url, "/registerError.html");
      } else {
        strcpy(m_url, "/registerError.html");
      }
    }
    // 如果是登录，直接判断
    // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
    else if (*(p + 1) == '2') {
      if (m_users.find(name) != m_users.end() && m_users[name] == password)
        strcpy(m_url, "/welcome.html");
      else
        strcpy(m_url, "/logError.html");
    }
  }
  if (*(p + 1) == '0') {
    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/register.html");
    strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

    free(m_url_real);
  } else if (*(p + 1) == '1') {
    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/log.html");
    strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

    free(m_url_real);
  } else if (*(p + 1) == '5') {
    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/picture.html");
    strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

    free(m_url_real);
  } else if (*(p + 1) == '6') {
    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/video.html");
    strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

    free(m_url_real);
  } else if (*(p + 1) == '7') {
    char *m_url_real = (char *)malloc(sizeof(char) * 200);
    strcpy(m_url_real, "/fans.html");
    strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

    free(m_url_real);
  } else
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
  if (stat(m_real_file, &m_file_stat) < 0)
    return NO_RESOURCE;

  if (!(m_file_stat.st_mode & S_IROTH))
    return FORBIDDEN_REQUEST;

  if (S_ISDIR(m_file_stat.st_mode)) // 是个目录
    return BAD_REQUEST;
  int fd = open(m_real_file, O_RDONLY);
  m_file_address =
      (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  return FILE_REQUEST;
}
void http_conn::unmap() {
  if (m_file_address) {
    munmap(m_file_address, m_file_stat.st_size);
    m_file_address = 0;
  }
}
bool http_conn::write(int *saveErrno) {
  int tem = 0;
  if (bytes_to_send == 0) {
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    init();
    return true;
  }
  while (1) {
    tem = m_write_buf.TransFd(m_sockfd, saveErrno);
    if (tem < 0) {
      if (*saveErrno == EAGAIN) {
        modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
        return true;
      }
      unmap();
      return false;
    }
    bytes_have_send += tem;
    bytes_to_send -= tem;
    if (bytes_to_send <= 0) { // 发完了
      unmap();
      modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
      if (m_linger) {
        init();
        return true;
      } else {
        return false;
      }
    }
  }
}
void http_conn::add_status_line(int status, const char *title) {
  m_write_buf.Append("HTTP/1.1 " + to_string(status) + " " + title + "\r\n");
}
void http_conn::add_content_type() {
  m_write_buf.Append("Content-Type:" + string("text/html") + "\r\n");
}
void http_conn::add_headers(int content_len) {
  m_write_buf.Append("Content-Length:" + to_string(content_len) + "\r\n");
  m_write_buf.Append(("Connection:") +
                     string((m_linger == true) ? "keep-alive" : "close") +
                     "\r\n");
  m_write_buf.Append("\r\n");
}
void http_conn::add_content(const char *content) {
  m_write_buf.Append(content);
}
bool http_conn::process_write(HTTP_CODE ret) {
  switch (ret) {
  case INTERNAL_ERROR: {
    add_status_line(500, error_500_title);
    add_headers(strlen(error_500_form));
    add_content(error_500_form);
    break;
  }
  case BAD_REQUEST: {
    add_status_line(404, error_404_title);
    add_headers(strlen(error_404_form));
    add_content(error_404_form);
    break;
  }
  case FORBIDDEN_REQUEST: {
    add_status_line(403, error_403_title);
    add_headers(strlen(error_403_form));
    add_content(error_403_form);

    break;
  }
  case FILE_REQUEST: {
    add_status_line(200, ok_200_title);
    if (m_file_stat.st_size != 0) {
      add_headers(m_file_stat.st_size);

      m_write_buf.iov_[0].iov_base = m_write_buf.BeginPtr();
      m_write_buf.iov_[0].iov_len = m_write_buf.ReadableBytes();
      m_write_buf.iov_[1].iov_base = m_file_address;
      m_write_buf.iov_[1].iov_len = m_file_stat.st_size;
      m_write_buf.iovCnt_ = 2;
      bytes_to_send = m_write_buf.iov_[0].iov_len + m_file_stat.st_size;
      return true;
    } else {
      const char *ok_string = "<html><body></body></html>";
      add_headers(strlen(ok_string));
      add_content(ok_string);
      return false;
    }
  }
  default:
    return false;
  }
  m_write_buf.iov_[0].iov_base = m_write_buf.BeginPtr();
  m_write_buf.iov_[0].iov_len = m_write_buf.ReadableBytes();
  m_write_buf.iovCnt_ = 1;
  bytes_to_send = m_write_idx;
  return true;
}
void http_conn::process() {
  HTTP_CODE read_ret = process_read();
  if (read_ret == NO_REQUEST) {
    modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
    return;
  }
  bool write_ret = process_write(read_ret);
  if (!write_ret) {
    close_conn();
  }
  modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); // 等待写就绪
}