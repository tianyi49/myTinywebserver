#include "log.h"
#include <bits/types/struct_timeval.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
using namespace std;

Log::Log() {
  m_count = 0;
  m_is_async = false;
}
Log::~Log() {
  if (m_fp != nullptr)
    fclose(m_fp);
}
int Log::getNumberOfLines(char *filepath) {
  char flag;
  FILE *fp = fopen(filepath, "r");
  int count = 0;
  while (!feof(fp)) {

    flag = fgetc(fp);
    if (flag == '\n')
      count++;
  }

  // 因为最后一行没有换行符\n，所以需要在count补加1
  fclose(fp);
  return count;
}
// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size,
               int split_lines, int max_queue_size) {
  if (max_queue_size >= 1) {
    m_is_async = true;
    m_log_queue = new block_queue<string>(max_queue_size);
    pthread_t tid;
    pthread_create(&tid, NULL, &flush_log_thread, NULL); // 创建异步写入线程
  }
  m_close_log = close_log;
  m_log_buf_size = log_buf_size;
  m_split_lines = split_lines;
  m_buf = new char[m_log_buf_size];
  memset(m_buf, '\0', m_log_buf_size);

  time_t t = time(NULL);
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;
  const char *p = strrchr(file_name, '/');
  char log_full_name[256] = {0};
  if (p == NULL) {
    snprintf(log_full_name, 255, "%d_%02d_%02d_%s", &my_tm.tm_year + 1900,
             &my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
  } else {
    strcpy(log_name, p + 1);
    strncpy(dir_name, file_name, p - file_name + 1);
    snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name,
             my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
  }
  m_today = my_tm.tm_mday;
  m_fp = fopen(log_full_name, "a");
  if (m_fp == NULL) {
    return false;
  }
  m_count = getNumberOfLines(log_full_name);
  return true;
}
void Log::write_log(int level, const char *format, ...) {
  struct timeval now = {0, 0};
  gettimeofday(&now, NULL);
  time_t t = now.tv_sec;
  struct tm *sys_tm = localtime(&t);
  struct tm my_tm = *sys_tm;
  char s[16] = {0};
  switch (level) {
  case 0:
    strcpy(s, "[debug]:");
    break;
  case 1:
    strcpy(s, "[info]:");
    break;
  case 2:
    strcpy(s, "[warn]:");
    break;
  case 3:
    strcpy(s, "[erro]:");
    break;
  default:
    strcpy(s, "[info]:");
    break;
  }
  // 写入一个log，对m_count++, m_split_lines最大行数
  m_mutex.lock();
  m_count++;
  if (m_today != my_tm.tm_mday ||
      m_count % m_split_lines == 0) { // 新建日志文件
    char new_log[256] = {0};
    fflush(m_fp);
    fclose(m_fp);
    char tail[16] = {0};
    snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1,
             my_tm.tm_mday);
    if (m_today != my_tm.tm_mday) {
      snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
      m_today = my_tm.tm_mday;
      m_count = 0;
    } else {
      snprintf(new_log, 255, "%s%s%s_%lld", dir_name, tail, log_name,
               m_count / m_split_lines);
    }
    m_fp = fopen(new_log, "a");
  }
  m_mutex.unlock();

  // 真正写入
  va_list valist;
  va_start(valist, format);
  string log_str;
  m_mutex.lock();
  // 写入的具体时间内容格式
  int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                   my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                   my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
  int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valist);

  m_buf[m + n] = '\n';
  m_buf[m + n + 1] = '\0';
  log_str = m_buf;
  m_mutex.unlock();
  if (m_is_async && !m_log_queue->full()) {
    m_log_queue->push(log_str);
  } else {
    m_mutex.lock();
    fputs(log_str.c_str(), m_fp);
    m_mutex.unlock();
  }
  va_end(valist);
}

void Log::flush() {
  m_mutex.lock();
  fflush(m_fp);
  m_mutex.unlock();
}

void log_init(int async_flag) {
  if (1 == async_flag)
    Log::get_instance()->init("./log_record/ServerLog", 0, 2000, 1000000, 800);
  else
    Log::get_instance()->init("./log_record/ServerLog", 0, 2000, 1000000, 0);
}
int64_t get_current_millis(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
// 单线程异步写入测试
static int logs = 100 * 10000;
void single_thread_test() {
  printf("single_thread_test...\n");
  uint64_t start_ts = get_current_millis();
  for (int i = 0; i < logs; ++i) {
    LOG_INFO("log test %d\n", i);
  }
  uint64_t end_ts = get_current_millis();
  printf("1 million times logtest, time use %lums, %ldw logs/second\n",
         end_ts - start_ts, logs / (end_ts - start_ts) / 10);
}

int main() {
  log_init(1);
  single_thread_test();
  return 0;
}