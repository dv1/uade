#include <sys/poll.h>
#include <errno.h>

#include <unixatomic.h>


int atomic_close(int fd)
{
  while (1) {
    if (close(fd) < 0) {
      if (errno == EINTR)
	continue;
      return -1;
    }
    break;
  }
  return 0;
}


int atomic_dup2(int oldfd, int newfd)
{
  while (1) {
    if (dup2(oldfd, newfd) < 0) {
      if (errno == EINTR)
	continue;
      return -1;
    }
    break;
  }
  return newfd;
}


ssize_t atomic_read(int fd, const void *buf, size_t count)
{
  char *b = (char *) buf;
  ssize_t bytes_read = 0;
  int ret;
  while (bytes_read < count) {
    ret = read(fd, &b[bytes_read], count - bytes_read);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN) {
	if (poll(& (struct pollfd) {.fd = fd, .events = POLLIN}, 1, -1) == 0)
	  fprintf(stderr, "atomic_read: very strange. infinite poll() returned 0. report this!\n");
	continue;
      }
      return -1;
    }
    bytes_read += ret;
  }
  return bytes_read;
}


ssize_t atomic_write(int fd, const void *buf, size_t count)
{
  char *b = (char *) buf;
  ssize_t bytes_written = 0;
  int ret;
  while (bytes_written < count) {
    ret = write(fd, &b[bytes_written], count - bytes_written);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN) {
	if (poll(& (struct pollfd) {.fd = fd, .events = POLLOUT}, 1, -1) == 0)
	  fprintf(stderr, "atomic_write: very strange. infinite poll() returned 0. report this!\n");
	continue;
      }
      return -1;
    }
    bytes_written += ret;
  }
  return bytes_written;
}