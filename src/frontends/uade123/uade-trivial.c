#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

#include <uadeipc.h>
#include <strlrep.h>
#include <unixatomic.h>

static char configname[PATH_MAX];
static char modulename[PATH_MAX];
static char playername[PATH_MAX];
static char scorename[PATH_MAX];
static char uadename[PATH_MAX];

static pid_t uadepid = -1;

static int input_fd = -1;
static int output_fd = -1;

static void trivial_sigint(int sig);
static void trivial_cleanup(void);


static void trivial_cleanup(void)
{
  if (uadepid != -1) {
    kill(uadepid, SIGTERM);
    uadepid = -1;
  }
}


static void fork_exec(int uade_stdin, int uade_stdout)
{
  uadepid = fork();
  if (uadepid < 0) {
    fprintf(stderr, "fork failed: %s\n", strerror(errno));
    return;
  }
  if (uadepid == 0) {

    if (atomic_dup2(uade_stdin, 0) < 0) {
      fprintf(stderr, "can not dup stdin: %s\n", strerror(errno));
      abort();
    }
    if (atomic_dup2(uade_stdout, 1) < 0) {
      fprintf(stderr, "can not dup stdout: %s\n", strerror(errno));
      abort();
    }
    execlp(uadename, uadename, NULL);
    fprintf(stderr, "execlp failed: %s\n", strerror(errno));
    abort();
  }
}


static int get_string_arg(char *dst, size_t maxlen, const char *arg, int *i,
			  char *argv[], int *argc)
{
  if (strcmp(argv[*i], arg) == 0) {
    if ((*i + 1) >= *argc) {
      fprintf(stderr, "missing parameter for %s\n", argv[*i]);
      exit(-1);
    }
    strlcpy(dst, argv[*i + 1], maxlen);
    *i += 2;
    return 1;
  }
  return 0;
}


int main(int argc, char *argv[])
{
  int i;
  int forwardfiledes[2];
  int backwardfiledes[2];

  for (i = 1; i < argc;) {
    fprintf(stderr, "processing %s\n", argv[i]);
    if (get_string_arg(configname, sizeof(configname), "-c", &i, argv, &argc))
      continue;
    if (get_string_arg(modulename, sizeof(modulename), "-m", &i, argv, &argc))
      continue;
    if (get_string_arg(playername, sizeof(playername), "-p", &i, argv, &argc))
      continue;
    if (get_string_arg(scorename, sizeof(scorename), "-s", &i, argv, &argc))
      continue;
    if (get_string_arg(uadename, sizeof(uadename), "-u", &i, argv, &argc))
      continue;
    fprintf(stderr, "unknown arg: %s\n", argv[i]);
    exit(-1);
  }

#define CHECK_EXISTENCE(x, y) do { if ((x)[0] == 0) { fprintf(stderr, "must have %s\n", (y)); exit(-1); } } while (0)

  CHECK_EXISTENCE(configname, "config name");
  CHECK_EXISTENCE(modulename, "module name");
  CHECK_EXISTENCE(playername, "player name");
  CHECK_EXISTENCE(scorename, "score name");
  CHECK_EXISTENCE(uadename, "uade executable name");

  while (1) {
    if ((sigaction(SIGINT, & (struct sigaction) {.sa_handler = trivial_sigint}, NULL)) < 0) {
      if (errno == EINTR)
	continue;
      fprintf(stderr, "can not install signal handler SIGINT: %s\n", strerror(errno));
      exit(-1);
    }
    break;
  }

  if (pipe(forwardfiledes)) {
    fprintf(stderr, "can not create a pipe: %s\n", strerror(errno));
    exit(-1);
  }
  output_fd = forwardfiledes[1];
  if (pipe(backwardfiledes)) {
    fprintf(stderr, "can not create a pipe: %s\n", strerror(errno));
    exit(-1);
  }
  input_fd = forwardfiledes[0];

  fork_exec(forwardfiledes[0], backwardfiledes[1]);
  if (uadepid == -1)
    return -1;

  /* close fd that uade reads from */
  if (atomic_close(forwardfiledes[0]) < 0) {
    fprintf(stderr, "could not close forwardfiledes[0]\n");
    trivial_cleanup();
    exit(-1);
  }
  /* close fd that uade writes to */
  if (atomic_close(backwardfiledes[1]) < 0) {
    fprintf(stderr, "could not close backwardfiledes[1]\n");
    trivial_cleanup();
    exit(-1);
  }

  fprintf(stderr, "killing child (%d)\n", uadepid);
  kill(uadepid, SIGTERM);
  return 0;
}


static void trivial_sigint(int sig)
{
  trivial_cleanup();
  exit(-1);
}
