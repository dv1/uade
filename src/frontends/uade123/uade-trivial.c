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
  int forwardfds[2];
  int backwardfds[2];
  char url[64];

  if (pipe(forwardfds) != 0 || pipe(backwardfds) != 0) {
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
    /* close everything else but stdin, stdout, stderr, and in/out fds */
    for (fd = 3; fd < 64; fd++) {
      if (fd != forwardfds[0] && fd != backwardfds[1])
	atomic_close(fd);
    }
    /* give in/out fds as command line parameters to the uade process */
    snprintf(instr, sizeof(instr), "fd://%d", forwardfds[0]);
    snprintf(outstr, sizeof(outstr), "fd://%d", backwardfds[1]);
    execlp(uadename, uadename, "-i", instr, "-o", outstr, NULL);
    fprintf(stderr, "execlp failed: %s\n", strerror(errno));
    abort();
  }

  /* close fd that uade reads from and writes to */
  if (atomic_close(forwardfds[0]) < 0 || atomic_close(backwardfds[1]) < 0) {
    fprintf(stderr, "could not close uade fds: %s\n", strerror(errno));
    trivial_cleanup();
    exit(-1);
  }

  /* write destination */
  snprintf(url, sizeof(url), "fd://%d", forwardfds[1]);
  uade_set_output_destination(url);
  /* read source */
  snprintf(url, sizeof(url), "fd://%d", backwardfds[0]);
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
  uint8_t space[UADE_MAX_MESSAG_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

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

  if (uade_receive_message(um, sizeof(space)) <= 0) {
    fprintf(stderr, "can not receive acknowledgement from uade\n");
    goto cleanup;
  }
  if (um->msgtype == UADE_REPLY_CANT_PLAY) {
    fprintf(stderr, "uade refuses to play the song\n");
    goto cleanup;
  }
  if (um->msgtype != UADE_REPLY_CAN_PLAY) {
    fprintf(stderr, "unexpected reply from uade: %d\n", um->msgtype);
    goto cleanup;
  }

  play_loop();

  fprintf(stderr, "killing child (%d)\n", uadepid);
  kill(uadepid, SIGTERM);
  return 0;

 cleanup:
  trivial_cleanup();
  return -1;
}


static void play_loop(void)
{
  while (nanosleep(& (struct timespec) {.tv_sec = 1}, NULL) >= 0);
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
