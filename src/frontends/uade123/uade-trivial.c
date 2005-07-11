#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <ao/ao.h>

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>

static char configname[PATH_MAX];
static char modulename[PATH_MAX];
static char playername[PATH_MAX];
static char scorename[PATH_MAX];
static char uadename[PATH_MAX];

static int debug_mode = 0;
static int debug_trigger = 0;
static pid_t uadepid = -1;
static int uadeterminated = 0;

static int play_loop(void);
static void setup_sighandlers(void);
static void trivial_sigchld(int sig);
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
    if (debug_mode) {
      execlp(uadename, uadename, "-d", "-i", instr, "-o", outstr, NULL);
    } else {
      execlp(uadename, uadename, "-i", instr, "-o", outstr, NULL);
    }
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
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  for (i = 1; i < argc;) {
    if (get_string_arg(configname, sizeof(configname), "-c", &i, argv, &argc))
      continue;
    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
      debug_mode = 1;
      i ++;
      continue;
    }
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

  if (uade_send_string(UADE_COMMAND_CONFIG, configname)) {
    fprintf(stderr, "can not send config name\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_SCORE, scorename)) {
    fprintf(stderr, "can not send score name\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_PLAYER, playername)) {
    fprintf(stderr, "can not send player name\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_MODULE, modulename)) {
    fprintf(stderr, "can not send module name\n");
    goto cleanup;
  }

  if (uade_send_short_message(UADE_COMMAND_TOKEN)) {
    fprintf(stderr, "can not send token after module\n");
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

  if (uade_receive_short_message(UADE_COMMAND_TOKEN) < 0) {
    fprintf(stderr, "can not receive token after play ack\n");
    goto cleanup;
  }

  if (!play_loop())
    goto cleanup;

  fprintf(stderr, "killing child (%d)\n", uadepid);
  trivial_cleanup();
  return 0;

 cleanup:
  trivial_cleanup();
  return -1;
}


static int play_loop(void)
{
  int default_driver;
  ao_sample_format format;
  ao_device *libao_device;
  uint16_t *sm;
  int i;
  uint32_t *u32ptr;

  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  int left;

  ao_initialize();
  default_driver = ao_default_driver_id();

  format.bits = 16;
  format.channels = 2;
  format.rate = 44100;
  format.byte_format = AO_FMT_NATIVE;

  libao_device = ao_open_live(default_driver, &format, NULL);
  if (libao_device == NULL) {
    fprintf(stderr, "error opening device\n");
    return 0;
  }

  left = 0;
  enum uade_control_state state = UADE_S_STATE;

  while (1) {

    if (uadeterminated != 0)
      break;

    if (state == UADE_S_STATE) {

      if (left == 0) {
	
	if (debug_trigger == 1) {
	  if (uade_send_message(& (struct uade_msg) {.msgtype = UADE_COMMAND_ACTIVATE_DEBUGGER, .size = 0})) {
	    fprintf(stderr, "can not active debugger\n");
	    return 0;
	  }
	  debug_trigger = 0;
	}
	
	left = UADE_MAX_MESSAGE_SIZE - sizeof(*um);
	um->msgtype = UADE_COMMAND_READ;
	um->size = 4;
	* (uint32_t *) um->data = htonl(left);
	if (uade_send_message(um)) {
	  fprintf(stderr, "can not send read command\n");
	  return 0;
	}
	
	if (uade_send_short_message(UADE_COMMAND_TOKEN)) {
	  fprintf(stderr, "can not send token\n");
	  return 0;
	}

	state = UADE_R_STATE;
      }

    } else {

      if (uade_receive_message(um, sizeof(space)) <= 0) {
	fprintf(stderr, "can not receive events from uade\n");
	return 0;
      }
      
      switch (um->msgtype) {

      case UADE_COMMAND_TOKEN:
	state = UADE_S_STATE;
	break;

      case UADE_REPLY_DATA:
	sm = (uint16_t *) um->data;
	for (i = 0; i < um->size; i += 2) {
	  *sm = ntohs(*sm);
	  sm++;
	}
	
	if (!ao_play(libao_device, um->data, um->size)) {
	  fprintf(stderr, "libao error detected.\n");
	  return 0;
	}
	
	left -= um->size;
	break;
	
      case UADE_REPLY_FORMATNAME:
	uade_check_fix_string(um, 128);
	fprintf(stderr, "got formatname: %s\n", (uint8_t *) um->data);
	break;
	
      case UADE_REPLY_MODULENAME:
	uade_check_fix_string(um, 128);
	fprintf(stderr, "got modulename: %s\n", (uint8_t *) um->data);
	break;
	
      case UADE_REPLY_PLAYERNAME:
	uade_check_fix_string(um, 128);
	fprintf(stderr, "got playername: %s\n", (uint8_t *) um->data);
	break;
	
      case UADE_REPLY_SUBSONG_INFO:
	if (um->size != 12) {
	  fprintf(stderr, "subsong info: too short a message\n");
	  exit(-1);
	}
	u32ptr = (uint32_t *) um->data;
	fprintf(stderr, "got subsong info: min: %d max: %d cur: %d\n", u32ptr[0], u32ptr[1], u32ptr[2]);
	break;
	
      default:
	fprintf(stderr, "expected sound data. got %d.\n", um->msgtype);
	return 0;
      }
    }
  }

  return 1;
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
  while (1) {
    if ((sigaction(SIGCHLD, & (struct sigaction) {.sa_handler = trivial_sigchld, .sa_flags = SA_NOCLDSTOP}, NULL)) < 0) {
      if (errno == EINTR)
	continue;
      fprintf(stderr, "can not install signal handler SIGCHLD: %s\n", strerror(errno));
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


static void trivial_sigchld(int sig)
{
  pid_t process;
  int status;
  int successful;
  process = waitpid(-1, &status, WNOHANG);
  if (process == 0)
    return;
  successful = (WEXITSTATUS(status) == 0);
  fprintf(stderr, "uade exited %ssuccessfully\n", successful == 1 ? "" : "un");
  if (uadepid != -1 && process != uadepid)
    fprintf(stderr, "interesting sigchld: uadepid = %d and processpid = %d\n",
	    uadepid, process);
  uadepid = -1;
  uadeterminated = 1;
}


static void trivial_sigint(int sig)
{
  if (debug_mode == 1) {
    debug_trigger = 1;
    return;
  } else {
    trivial_cleanup();
    exit(-1);
  }
}
