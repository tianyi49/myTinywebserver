/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */

#ifndef BUFFER_H
#define BUFFER_H
#include <assert.h>
#include <atomic>
#include <cstddef>
#include <cstring> //perror
#include <iostream>
#include <sys/uio.h> //readv
#include <unistd.h>  // write
#include <vector>    //readv
class Buffer {
public:
  Buffer(int initBuffSize = 1024);
  virtual ~Buffer() = default;

  size_t WritableBytes() const;
  size_t ReadableBytes() const;
  size_t PrependableBytes() const;

  const char *Peek() const;
  void EnsureWriteable(size_t len);
  void HasWritten(size_t len);

  void Retrieve(size_t len);
  void RetrieveUntil(const char *end);

  void RetrieveAll();
  std::string RetrieveAllToStr();

  const char *BeginWriteConst() const;
  char *BeginWrite();

  void Append(const std::string &str);
  void Append(const char *str, size_t len);
  void Append(const void *data, size_t len);
  void Append(const Buffer &buff);

  virtual ssize_t TransFd(int fd, int *Errno) = 0;
  char &operator[](size_t n) { return buffer_[n]; }
  char *BeginPtr();

protected:
  const char *BeginPtr() const;
  void MakeSpace_(size_t len);

  std::vector<char> buffer_;
  std::atomic<std::size_t> readPos_; // 这里设置原子变量应该是没有必要的
  std::atomic<std::size_t> writePos_;
};

class ReadBuffer : public Buffer {
public:
  ReadBuffer(int initBuffSize = 1024) : Buffer(initBuffSize) {}
  ~ReadBuffer() = default;
  ssize_t TransFd(int fd, int *Errno) override;
};
class WriteBuffer : public Buffer {
public:
  WriteBuffer(int initBuffSize = 1024) : Buffer(initBuffSize) {}
  ~WriteBuffer() = default;
  ssize_t TransFd(int fd, int *Errno) override;
  int iovCnt_;
  struct iovec iov_[2];
};
#endif // BUFFER_H