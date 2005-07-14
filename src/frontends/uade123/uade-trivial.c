#include <assert.h>
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
#include <dirent.h>
#include <sys/stat.h>

#include <ao/ao.h>

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>
#include <uadeconfig.h>
#include <amifilemagic.h>
#include <uadeformats.h>

#include "playlist.h"

static char basedir[PATH_MAX];

static char configname[PATH_MAX];
static char playername[PATH_MAX];
static char scorename[PATH_MAX];
static char uadename[PATH_MAX];

static int debug_mode = 0;
static int debug_trigger = 0;
static uint8_t fileformat_buf[5122];
static void *format_ds = NULL;
static int format_ds_size;
static ao_device *libao_device;
static pid_t uadepid = -1;
static int uadeterminated = 0;

static int song_end_trigger = 0;

static int play_loop(void);
static void set_subsong(struct uade_msg *um, int subsong);
static void setup_sighandlers(void);
static int test_song_end_trigger(void);
static void trivial_sigchld(int sig);
static void trivial_sigint(int sig);
static void trivial_cleanup(void);


static int audio_init(void)
{
  int default_driver;
  ao_sample_format format;

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
  return 1;
}


static char *fileformat_detection(const char *modulename)
{
  struct stat st;
  char extension[11];
  FILE *f;
  size_t readed;
  char *candidates;
  char *t, *tn;
  int len;
  static int warnings = 1;

  if ((f = fopen(modulename, "r")) == NULL) {
    fprintf(stderr, "can not open module: %s\n", modulename);
    return NULL;
  }
  if (fstat(fileno(f), &st)) {
    fprintf(stderr, "very weird stat error: %s (%s)\n", modulename, strerror(errno));
    exit(-1);
  }
  readed = fread(fileformat_buf, 1, sizeof(fileformat_buf), f);
  fclose(f);
  if (readed == 0)
    return NULL;
  memset(&fileformat_buf[readed], 0, sizeof(fileformat_buf) - readed);
  extension[0] = 0;
  filemagic(fileformat_buf, extension, st.st_size);

  fprintf(stderr, "deduced extension: %s\n", extension);

  if (format_ds == NULL) {
    char formatsfile[PATH_MAX];
    snprintf(formatsfile, sizeof(formatsfile), "%s/uadeformats", basedir);
    if ((format_ds = uade_read_uadeformats(&format_ds_size, formatsfile)) == NULL) {
      if (warnings)
	fprintf(stderr, "tried to load uadeformats file from %s, but failed\n", formatsfile);
      warnings = 0;
      return NULL;
    }
  }

  /* if filemagic found a match, we'll use player plugins associated with
     that extension. if filemagic didn't find a match, we'll try to parse
     pre- and postfixes from the modulename */

  if (extension[0]) {
    /* get a ',' separated list of player plugin candidates for this
       extension */
    candidates = uade_get_playername(extension, format_ds, format_ds_size);
    if (candidates)
      return candidates;
    fprintf(stderr, "interesting. a deduced file extension is not on the uadeformats list\n");
  }

  /* magic wasn't able to deduce the format, so we'll try prefix and postfix
     from modulename */
  t = strrchr(modulename, (int) '/');
  if (t == NULL) {
    t = (char *) modulename;
  } else {
    t++;
  }

  /* try prefix first */
  tn = strchr(t, '.');
  if (tn == NULL) {
    fprintf(stderr, "unknown format: %s\n", modulename);
    return NULL;
  }
  len = ((intptr_t) tn) - ((intptr_t) t);
  if (len < sizeof(extension)) {
    memcpy(extension, t, len);
    extension[len] = 0;
    candidates = uade_get_playername(extension, format_ds, format_ds_size);
    if (candidates)
      return candidates;
  }

  /* prefix didn't match anything. trying postfix */
  t = strrchr(t, '.');
  if (strlcpy(extension, t, sizeof(extension)) >= sizeof(extension)) {
    /* too long to be an extension */
    fprintf(stderr, "unknown format: %s\n", modulename);
    return NULL;
  }
  return uade_get_playername(extension, format_ds, format_ds_size);
}


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
  int recursivemode = 0;
  int handleswitches = 1;
  char modulename[PATH_MAX];
  int playernamegiven = 0;
  struct playlist playlist;
  char tmpstr[256];
  long subsong = -1;

  if (!playlist_init(&playlist)) {
    fprintf(stderr, "can not initialize playlist\n");
    exit(-1);
  }

  for (i = 1; i < argc;) {

    if (get_string_arg(basedir, sizeof(basedir), "-b", &i, argv, &argc))
      continue;
    if (get_string_arg(configname, sizeof(configname), "-c", &i, argv, &argc))
      continue;
    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
      debug_mode = 1;
      i++;
      continue;
    }
    if (get_string_arg(modulename, sizeof(modulename), "-m", &i, argv, &argc)) {
      playlist_add(&playlist, modulename, 0);
      continue;
    }
    if (get_string_arg(playername, sizeof(playername), "-p", &i, argv, &argc)) {
      playernamegiven = 1;
      continue;
    }
    if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
      recursivemode = 1;
      i++;
      continue;
    }
    if (get_string_arg(tmpstr, sizeof(tmpstr), "-s", &i, argv, &argc) ||
	get_string_arg(tmpstr, sizeof(tmpstr), "-sub", &i, argv, &argc)) {
      char *endptr;
      if (tmpstr[0] == 0) {
	fprintf(stderr, "uade123: subsong string must be non-empty\n");
	exit(-1);
      }
      subsong = strtol(tmpstr, &endptr, 10);
      if (*endptr != 0 || subsong < 0 || subsong > 255) {
	fprintf(stderr, "uade123: illegal subsong string: %s\n", tmpstr);
	exit(-1);
      }
      continue;
    }
    if (get_string_arg(scorename, sizeof(scorename), "-S", &i, argv, &argc)) {
      continue;
    }
    if (get_string_arg(uadename, sizeof(uadename), "-u", &i, argv, &argc))
      continue;
    if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--shuffle") == 0) {
      playlist_random(&playlist, 1);
      i++;
      continue;
    }
    if (strcmp(argv[i], "--") == 0) {
      handleswitches = 0;
      i++;
      continue;
    }
    if (handleswitches && argv[i][0] == '-') {
      fprintf(stderr, "unknown arg: %s\n", argv[i]);
      exit(-1);
    }
    playlist_add(&playlist, argv[i], recursivemode);
    i++;
  }

  if (basedir[0] == 0)
    strlcpy(basedir, UADE_CONFIG_BASE_DIR, sizeof(basedir));

#define CHECK_EXISTENCE(x, y) do { if ((x)[0] == 0) { fprintf(stderr, "must have %s\n", (y)); exit(-1); } } while (0)

  if (basedir[0]) {
    DIR *bd;
    if ((bd = opendir(basedir)) == NULL) {
      fprintf(stderr, "could not access dir %s: %s\n", basedir, strerror(errno));
      exit(-1);
    }
    closedir(bd);
    if (configname[0] == 0)
      snprintf(configname, sizeof(configname), "%s/uaerc", basedir);
    if (scorename[0] == 0)
      snprintf(scorename, sizeof(scorename), "%s/score", basedir);
    if (uadename[0] == 0)
      snprintf(uadename, sizeof(uadename), "%s/uadecore", basedir);
  } else {
    CHECK_EXISTENCE(configname, "config name");
    CHECK_EXISTENCE(scorename, "score name");
    CHECK_EXISTENCE(uadename, "uade executable name");
  }

  if (access(configname, R_OK)) {
    fprintf(stderr, "could not read %s: %s\n", configname, strerror(errno));
    exit(-1);
  }
  if (access(scorename, R_OK)) {
    fprintf(stderr, "could not read %s: %s\n", scorename, strerror(errno));
    exit(-1);
  }
  if (access(uadename, X_OK)) {
    fprintf(stderr, "could not execute %s: %s\n", uadename, strerror(errno));
    exit(-1);
  }

  setup_sighandlers();

  fork_exec_uade();

  if (!audio_init())
    goto cleanup;
  
  if (uade_send_string(UADE_COMMAND_CONFIG, configname)) {
    fprintf(stderr, "can not send config name\n");
    goto cleanup;
  }

  while (playlist_get_next(modulename, sizeof(modulename), &playlist)) {
    char **playernames = NULL;
    int nplayers;

    if (access(modulename, R_OK)) {
      fprintf(stderr, "could not read %s: %s\n", modulename, strerror(errno));
      exit(-1);
    }

    nplayers = 1;
    if (playernamegiven == 0) {
      char *t, *tn;
      char *candidates;
      size_t len;

      candidates = fileformat_detection(modulename);

      fprintf(stderr, "got candidates: %s\n", candidates);

      nplayers = 1;
      t = candidates;
      while ((t = strchr(t, (int) ','))) {
	nplayers++;
	t++;
      }

      playernames = malloc(sizeof(playernames[0]) * nplayers);
      
      t = candidates;
      for (i = 0; i < nplayers; i++) {
	tn = strchr(t, (int) ',');
	if (tn == NULL) {
	  len = strlen(t);
	} else {
	  len = ((intptr_t) tn) - ((intptr_t) t);
	}
	playernames[i] = malloc(len + 1);
	if (playernames[i] == NULL) {
	  fprintf(stderr, "out of memory.. damn it\n");
	  exit(-1);
	}
	memcpy(playernames[i], t, len);
	playernames[i][len] = 0;
	t = tn;
      }

      if (nplayers < 1) {
	fprintf(stderr, "skipping file with unknown format: %s\n", modulename);
	continue;
      }
      if (nplayers > 1) {
	fprintf(stderr, "multiple players not supported yet\n");
	continue;
      }

      if (strcmp(playernames[0], "custom") == 0) {
	strlcpy(playername, modulename, sizeof(playername));
	modulename[0] = 0;
      } else {
	snprintf(playername, sizeof(playername), "%s/players/%s", basedir, playernames[0]);
      }
    }

    if (playername[0]) {
      if (access(playername, R_OK)) {
	fprintf(stderr, "could not read %s: %s\n", playername, strerror(errno));
	exit(-1);
      }
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
      fprintf(stderr, "uade123: can not receive token after play ack\n");
      goto cleanup;
    }

    if (subsong >= 0)
      set_subsong(um, subsong);

    if (!play_loop())
      goto cleanup;

    if (playernames != NULL) {
      for (i = 0; i < nplayers; i++)
	free(playernames[i]);
      free(playernames);
    }
  }

  fprintf(stderr, "killing child (%d)\n", uadepid);
  trivial_cleanup();
  return 0;

 cleanup:
  trivial_cleanup();
  return -1;
}


static int play_loop(void)
{
  uint16_t *sm;
  int i;
  uint32_t *u32ptr;

  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  int left;
  int song_end = 0;
  int next_song = 0;
  int ret;
  int cur_sub = -1, max_sub = -1;
  int tailbytes = 0;
  int playbytes;
  char *reason;

  test_song_end_trigger(); /* clear a pending SIGINT */

  left = 0;
  enum uade_control_state state = UADE_S_STATE;

  while (next_song == 0) {

    if (uadeterminated)
      return 0;

    if (state == UADE_S_STATE) {

      if (left == 0) {

	if (debug_trigger == 1) {
	  if (uade_send_message(& (struct uade_msg) {.msgtype = UADE_COMMAND_ACTIVATE_DEBUGGER, .size = 0})) {
	    fprintf(stderr, "can not active debugger\n");
	    return 0;
	  }
	  debug_trigger = 0;
	}

	if (song_end) {
	  if (cur_sub != -1 && max_sub != -1) {
	    cur_sub++;
	    if (cur_sub >= max_sub) {
	      song_end_trigger = 1;
	    } else {
	      song_end = 0;
	      *um = (struct uade_msg) {.msgtype = UADE_COMMAND_CHANGE_SUBSONG,
				       .size = 4};
	      * (uint32_t *) um->data = htonl(cur_sub);
	      if (uade_send_message(um)) {
		fprintf(stderr, "could not change subsong\n");
		exit(-1);
	      }
	    }
	  } else {
	    song_end_trigger = 1;
	  }
	}

	/* check if control-c was pressed */
	if (song_end_trigger) {
	  next_song = 1;
	  if (uade_send_short_message(UADE_COMMAND_REBOOT)) {
	    fprintf(stderr, "can not send reboot\n");
	    return 0;
	  }
	  goto sendtoken;
	}

	left = UADE_MAX_MESSAGE_SIZE - sizeof(*um);
	um->msgtype = UADE_COMMAND_READ;
	um->size = 4;
	* (uint32_t *) um->data = htonl(left);
	if (uade_send_message(um)) {
	  fprintf(stderr, "can not send read command\n");
	  return 0;
	}

      sendtoken:
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

	if (song_end) {
	  if (tailbytes > 0) {
	    playbytes = tailbytes;
	    tailbytes = 0;
	  } else {
	    playbytes = 0;
	  }
	} else {
	  playbytes = um->size;
	}
	if (!ao_play(libao_device, um->data, playbytes)) {
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

      case UADE_REPLY_SONG_END:
	song_end = 1;
	if (um->size < 5) {
	  fprintf(stderr, "illegal song end reply\n");
	  exit(-1);
	}
	i = 0;
	reason = &((uint8_t *) um->data)[4];
	while (reason[i] && i < (um->size - 4))
	  i++;
	if (reason[i] != 0 || (i != (um->size - 5))) {
	  fprintf(stderr, "broken reason string with song end notice\n");
	  exit(-1);
	}
	fprintf(stderr, "got song end (%s)\n", reason);
	tailbytes = ntohl(* (uint32_t *) um->data);
	break;

      case UADE_REPLY_SUBSONG_INFO:
	if (um->size != 12) {
	  fprintf(stderr, "subsong info: too short a message\n");
	  exit(-1);
	}
	u32ptr = (uint32_t *) um->data;
	fprintf(stderr, "got subsong info: min: %d max: %d cur: %d\n", u32ptr[0], u32ptr[1], u32ptr[2]);
	cur_sub = u32ptr[2];
	max_sub = u32ptr[1];
	break;
	
      default:
	fprintf(stderr, "uade123: expected sound data. got %d.\n", um->msgtype);
	return 0;
      }
    }
  }

  do {
    ret = uade_receive_message(um, sizeof(space));
    if (ret < 0) {
      fprintf(stderr, "uade123: can not receive events (TOKEN) from uade\n");
      return 0;
    }
    if (ret == 0) {
      fprintf(stderr, "uade123: end of input after reboot\n");
      return 0;
    }
  } while (um->msgtype != UADE_COMMAND_TOKEN);

  return 1;
}


static void set_subsong(struct uade_msg *um, int subsong)
{
  assert(subsong > 0 && subsong < 256);
  *um = (struct uade_msg) {.msgtype = UADE_COMMAND_SET_SUBSONG, .size = 4};
  * (uint32_t *) um->data = htonl(subsong);
  if (uade_send_message(um) < 0) {
    fprintf(stderr, "could not set subsong\n");
    exit(-1);
  }
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


/* test song_end_trigger by taking care of mutual exclusion with SIGINT */
static int test_song_end_trigger(void)
{
  int ret;
  sigset_t set;
  if (sigemptyset(&set))
    goto sigerr;
  if (sigaddset(&set, SIGINT))
    goto sigerr;
  if (sigprocmask(SIG_BLOCK, &set, NULL))
    goto sigerr;
  ret = song_end_trigger;
  song_end_trigger = 0;
  if (sigprocmask(SIG_UNBLOCK, &set, NULL))
    goto sigerr;
  return ret;

 sigerr:
  fprintf(stderr, "signal hell\n");
  exit(-1);
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
  }
  song_end_trigger = 1;
}
