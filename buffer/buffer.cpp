/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */
#include "buffer.h"

Buffer::Buffer(int initBuffSize)
    : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

size_t Buffer::ReadableBytes() const { return writePos_ - readPos_; }
size_t Buffer::WritableBytes() const { return buffer_.size() - writePos_; }

size_t Buffer::PrependableBytes() const { return readPos_; }

const char *Buffer::Peek() const { return BeginPtr() + readPos_; }

void Buffer::Retrieve(size_t len) {
  assert(len <= ReadableBytes());
  readPos_ += len;
}

void Buffer::RetrieveUntil(const char *end) {
  assert(Peek() <= end);
  Retrieve(end - Peek());
}

void Buffer::RetrieveAll() {
  bzero(&buffer_[0], buffer_.size());
  readPos_ = 0;
  writePos_ = 0;
}

std::string Buffer::RetrieveAllToStr() {
  std::string str(Peek(), ReadableBytes());
  RetrieveAll();
  return str;
}

const char *Buffer::BeginWriteConst() const { return BeginPtr() + writePos_; }

char *Buffer::BeginWrite() { return BeginPtr() + writePos_; }

void Buffer::HasWritten(size_t len) { writePos_ += len; }

void Buffer::Append(const std::string &str) {
  Append(str.data(), str.length());
}

void Buffer::Append(const void *data, size_t len) {
  assert(data);
  Append(static_cast<const char *>(data), len);
}

void Buffer::Append(const char *str, size_t len) {
  assert(str);
  EnsureWriteable(len);
  std::copy(str, str + len, BeginWrite()); // 确保有足够的空间后真正的写入
  HasWritten(len);
}

void Buffer::Append(const Buffer &buff) {
  Append(buff.Peek(), buff.ReadableBytes());
}

void Buffer::EnsureWriteable(size_t len) {
  if (WritableBytes() < len) {
    MakeSpace_(len);
  }
  assert(WritableBytes() >= len);
}

ssize_t ReadBuffer::TransFd(int fd, int *saveErrno) {
  char buff[65535];
  struct iovec iov[2];
  const size_t writable = WritableBytes();
  /* 分散读， 保证数据全部读完 */

  iov[0].iov_base = BeginPtr() + writePos_;
  iov[0].iov_len = writable;
  iov[1].iov_base = buff;
  iov[1].iov_len = sizeof(buff);

  const ssize_t len = readv(fd, iov, 2);
  if (len < 0) {
    *saveErrno = errno;
  } else if (static_cast<size_t>(len) <= writable) {
    writePos_ += len;
  } else { // 扩容
    writePos_ = buffer_.size();
    Append(buff, len - writable);
  }
  return len;
}

ssize_t WriteBuffer::TransFd(int fd, int *saveErrno) {
  const ssize_t len = writev(fd, iov_, iovCnt_);
  if (len <= 0) {
    *saveErrno = errno;
  } else if (static_cast<size_t>(len) > iov_[0].iov_len) {
    iov_[1].iov_base = (uint8_t *)iov_[1].iov_base + (len - iov_[0].iov_len);
    iov_[1].iov_len -= (len - iov_[0].iov_len);
    if (iov_[0].iov_len) {
      RetrieveAll();
      iov_[0].iov_len = 0;
    }
  } else {
    iov_[0].iov_base = (uint8_t *)iov_[0].iov_base + len;
    iov_[0].iov_len -= len;
    Retrieve(len);
  }
  return len;
}

char *Buffer::BeginPtr() { return &*buffer_.begin(); }

const char *Buffer::BeginPtr() const { return &*buffer_.begin(); }

void Buffer::MakeSpace_(size_t len) {
  if (WritableBytes() + PrependableBytes() < len) { // 扩容
    buffer_.resize(writePos_ + len + 1);
  } else { // 腾出已读的数据的空间，直接将已写入的数据复制到vector最前面
    size_t readable = ReadableBytes();
    std::copy(BeginPtr() + readPos_, BeginPtr() + writePos_, BeginPtr());
    readPos_ = 0;
    writePos_ = readPos_ + readable;
    assert(readable == ReadableBytes());
  }
}