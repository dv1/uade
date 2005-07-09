#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <unixsupport.h>

void uade_portable_initializations(void)
{
  while (1) {
    if ((sigaction(SIGINT, & (struct sigaction) {.sa_handler = SIG_IGN}, NULL)) < 0) {
      if (errno == EINTR)
	continue;
      fprintf(stderr, "can not ignore SIGINT: %s\n", strerror(errno));
      exit(-1);
    }
    break;
  }
}
