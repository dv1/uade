#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <unixsupport.h>

void uade_portable_initializations(void)
{
  int signals[] = {SIGINT, -1};
  int *signum = signals;
  while (*signum != -1) {
    while (1) {
      if ((sigaction(*signum, & (struct sigaction) {.sa_handler = SIG_IGN}, NULL)) < 0) {
	if (errno == EINTR)
	  continue;
	fprintf(stderr, "can not ignore signal %d: %s\n", *signum, strerror(errno));
	exit(-1);
      }
      break;
    }
    signum++;
  }
}
