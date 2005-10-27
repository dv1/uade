#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>

#include "uade123.h"

static struct termios old_terminal;


static void uade_restore_terminal(void)
{
  tcsetattr(0, TCSANOW, &old_terminal);
}


void pause_terminal(void)
{
  struct pollfd pfd = {.fd = 0, .events = POLLIN};
  char c;
  int ret;
  tprintf("\nPaused. Press any key to continue...\n");
  while (1) {
    ret = poll(&pfd, 1, -1);
    if (ret < 0) {
      if (errno == EINTR)
	continue;
      perror("\nuade123: poll error");
      exit(-1);
    }
    if (ret == 0)
      continue;
    ret = read(0, &c, 1);
    if (ret < 0) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
    }
    break;
  }
  tprintf("\n");
}


int poll_terminal(void)
{
  struct pollfd pfd = {.fd = 0, .events = POLLIN};
  char c = 0;
  int ret;
  ret = poll(&pfd, 1, 0);
  if (ret > 0) {
    ret = read(0, &c, 1);
    if (ret <= 0)
      c = 0;
  }
  return c;
}


void setup_terminal(void)
{
  struct termios tp;
  if (tcgetattr(0, &old_terminal)) {
    perror("uade123: can't setup interactive mode");
    exit(-1);
  }
  atexit(uade_restore_terminal);
  tp = old_terminal;
  tp.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL);
  if (tcsetattr(0, TCSAFLUSH, &tp)) {
    perror("uade123: can't setup interactive mode (tcsetattr())");
    exit(-1);
  }
}
