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


ssize_t atomic_write(int fd, const void *buf, size_t count)
{
  char *b = (char *) buf;
  size_t bytes_written = 0;
  int ret;
  while (bytes_written < count) {
    ret = write(fd, &b[bytes_written], count - bytes_written);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN) {
	if (poll(& (struct pollfd) {.fd = fd, .events = POLLOUT}, 1, -1) == 0)
	  fprintf(stderr, "very strange. infinite poll() returned 0. report this!\n");
	continue;
      }
      if (bytes_written == 0)
        bytes_written = -1;
      break;
    }
    bytes_written += ret;
  }
  return bytes_written;
}
