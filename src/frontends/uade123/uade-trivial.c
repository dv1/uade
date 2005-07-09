#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>

static char configname[PATH_MAX];
static char modulename[PATH_MAX];
static char playername[PATH_MAX];
static char scorename[PATH_MAX];
static char uadename[PATH_MAX];

static pid_t uadepid = -1;

static void setup_sighandlers(void);
static void trivial_sigint(int sig);
static void trivial_cleanup(void);


static void fork_exec_uade(void)
{
  int forwardfiledes[2];
  int backwardfiledes[2];
  char url[64];

  if (pipe(forwardfiledes) != 0 || pipe(backwardfiledes) != 0) {
    fprintf(stderr, "can not create pipes: %s\n", strerror(errno));
    exit(-1);
  }
 
  uadepid = fork();
  if (uadepid < 0) {
    fprintf(stderr, "fork failed: %s\n", strerror(errno));
    exit(-1);
  }
  if (uadepid == 0) {
    int fd;
    char instr[32], outstr[32];
    for (fd = 3; fd < 64; fd++) {
      if (fd != forwardfiledes[0] && fd != backwardfiledes[1])
	atomic_close(fd);
    }
    snprintf(instr, sizeof(instr), "fd://%d", forwardfiledes[0]);
    snprintf(outstr, sizeof(outstr), "fd://%d", backwardfiledes[1]);
    execlp(uadename, uadename, "-i", instr, "-o", outstr, NULL);
    fprintf(stderr, "execlp failed: %s\n", strerror(errno));
    abort();
  }

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

  /* write destination */
  snprintf(url, sizeof(url), "fd://%d", forwardfiledes[1]);
  uade_set_output_destination(url);
  /* read source */
  snprintf(url, sizeof(url), "fd://%d", backwardfiledes[0]);
  uade_set_input_source(url);
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

  for (i = 1; i < argc;) {
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

  setup_sighandlers();

  fork_exec_uade();

  if (uade_send_string(UADE_COMMAND_CONFIG, configname) == 0) {
    fprintf(stderr, "can not send config name\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_SCORE, scorename) == 0) {
    fprintf(stderr, "can not send score name\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_PLAYER, playername) == 0) {
    fprintf(stderr, "can not send player name\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_MODULE, modulename) == 0) {
    fprintf(stderr, "can not send module name\n");
    goto cleanup;
  }

  if (uade_send_command(& (struct uade_control) {.command = UADE_COMMAND_PLAY, .size = 0}) < 0) {
    fprintf(stderr, "can not send play command\n");
    goto cleanup;
  }

  while (nanosleep(& (struct timespec) {.tv_sec = 1}, NULL) >= 0);

  fprintf(stderr, "killing child (%d)\n", uadepid);
  kill(uadepid, SIGTERM);
  return 0;

 cleanup:
  trivial_cleanup();
  return -1;
}


static void setup_sighandlers(void)
{
  while (1) {
    if ((sigaction(SIGINT, & (struct sigaction) {.sa_handler = trivial_sigint}, NULL)) < 0) {
      if (errno == EINTR)
	continue;
      fprintf(stderr, "can not install signal handler SIGINT: %s\n", strerror(errno));
      exit(-1);
    }
    break;
  }
}


static void trivial_cleanup(void)
{
  if (uadepid != -1) {
    kill(uadepid, SIGTERM);
    uadepid = -1;
  }
}


static void trivial_sigint(int sig)
{
  trivial_cleanup();
  exit(-1);
}
