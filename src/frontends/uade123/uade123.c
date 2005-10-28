/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>
#include <uadeconfig.h>
#include <amifilemagic.h>
#include <uadeformats.h>

#include "uade123.h"
#include "playlist.h"
#include "effects.h"
#include "playloop.h"
#include "audio.h"
#include "config.h"
#include "terminal.h"


int uade_debug_trigger;
int uade_force_filter;
int uade_filter_state;
int uade_ignore_player_check;
int uade_info_mode;
char *uade_interpolation_mode;
double uade_jump_pos = 0.0;
int uade_no_output;
char uade_output_file_format[16];
char uade_output_file_name[PATH_MAX];
int uade_one_subsong_per_file;
float uade_panning_value;
struct playlist uade_playlist;
int uade_recursivemode;
int uade_terminated;
FILE *uade_terminal_file;
int uade_terminal_mode = 1;
int uade_use_filter = FILTER_MODEL_A1200;
int uade_use_panning;
int uade_silence_timeout = 20; /* -1 is infinite */
int uade_song_end_trigger;
int uade_subsong_timeout = 512;
int uade_timeout = -1;
int uade_verbose_mode;


static char basedir[PATH_MAX];
static int debug_mode;
static uint8_t fileformat_buf[5122];
static void *format_ds = NULL;
static int format_ds_size;
static pid_t uadepid;
static char uadename[PATH_MAX];


static void print_help(void);
static void send_interpolation_command(void);
static void set_subsong(struct uade_msg *um, int subsong);
static void setup_sighandlers(void);
ssize_t stat_file_size(const char *name);
static void trivial_sigchld(int sig);
static void trivial_sigint(int sig);
static void trivial_cleanup(void);


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

  debug("%s: deduced extension: %s\n", modulename, extension);

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


int main(int argc, char *argv[])
{
  int i;
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;
  char configname[PATH_MAX] = "";
  char modulename[PATH_MAX] = "";
  char playername[PATH_MAX] = "";
  char scorename[PATH_MAX] = "";
  int playernamegiven = 0;
  char tmpstr[PATH_MAX + 256];
  long subsong = -1;
  int have_modules = 0;
  int ret;
  char *endptr;
  int uade_no_song_end = 0;
  int config_loaded;

#define OPT_FILTER       0x100
#define OPT_FORCE_LED    0x101
#define OPT_INTERPOLATOR 0x102
#define OPT_STDERR       0x103
#define OPT_NO_SONG_END  0x104

  struct option long_options[] = {
    {"debug", 0, NULL, 'd'},
    {"get-info", 0, NULL, 'g'},
    {"filter", 2, NULL, OPT_FILTER},
    {"force-led", 1, NULL, OPT_FORCE_LED},
    {"help", 0, NULL, 'h'},
    {"ignore", 0, NULL, 'i'},
    {"interpolator", 1, NULL, OPT_INTERPOLATOR},
    {"jump", 1, NULL, 'j'},
    {"keys", 0, NULL, 'k'},
    {"list", 1, NULL, '@'},
    {"no-filter", 0, NULL, 'n'},
    {"no-song-end", 0, NULL, OPT_NO_SONG_END},
    {"no-keys", 0, NULL, 'K'},
    {"one", 0, NULL, '1'},
    {"panning", 1, NULL, 'p'},
    {"recursive", 0, NULL, 'r'},
    {"shuffle", 0, NULL, 'z'},
    {"silence-timeout", 1, NULL, 'y'},
    {"stderr", 0, NULL, OPT_STDERR},
    {"subsong", 1, NULL, 's'},
    {"subsong-timeout", 1, NULL, 'w'},
    {"timeout", 1, NULL, 't'},
    {"verbose", 0, NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  if (!playlist_init(&uade_playlist)) {
    fprintf(stderr, "can not initialize playlist\n");
    exit(-1);
  }

  config_loaded = 0;
  if (getenv("HOME") != NULL) {
    snprintf(tmpstr, sizeof(tmpstr), "%s/.uade2/uade.conf", getenv("HOME"));
    config_loaded = load_config(tmpstr);
  }
  if (config_loaded == 0)
    config_loaded = load_config(UADE_CONFIG_BASE_DIR "/uade.conf");
  if (config_loaded == 0)
    fprintf(stderr, "Not able to load uade.conf from ~/.uade2/uade.conf or %s/uade.conf\n", UADE_CONFIG_BASE_DIR);

#define GET_OPT_STRING(x) if (strlcpy((x), optarg, sizeof(x)) >= sizeof(x)) {\
	fprintf(stderr, "too long a string for option %c\n", ret); \
         exit(-1); \
      }

  while ((ret = getopt_long(argc, argv, "@:1b:c:de:f:ghij:kKm:np:P:rs:S:t:u:vw:y:z", long_options, 0)) != -1) {
    switch (ret) {
    case '@':
      do {
	FILE *listfile = fopen(optarg, "r");
	if (listfile == NULL) {
	  fprintf(stderr, "can not open list file: %s\n", optarg);
	  exit(-1);
	}
	while ((fgets(tmpstr, sizeof(tmpstr), listfile)) != NULL) {
	  if (tmpstr[0] == '#')
	    continue;
	  if (tmpstr[strlen(tmpstr) - 1] == '\n')
	    tmpstr[strlen(tmpstr) - 1] = 0;
	  playlist_add(&uade_playlist, tmpstr, 0);
	}
	fclose(listfile);
	have_modules = 1;
      } while (0);
      break;
    case '1':
      uade_one_subsong_per_file = 1;
      break;
    case 'b':
      GET_OPT_STRING(basedir);
      break;
    case 'c':
      GET_OPT_STRING(configname);
      break;
    case 'd':
      debug_mode = 1;
      uade_debug_trigger = 1;
      break;
    case 'e':
      GET_OPT_STRING(uade_output_file_format);
      break;
    case 'f':
      GET_OPT_STRING(uade_output_file_name);
      break;
    case 'g':
      uade_info_mode = 1;
      uade_no_output = 1;
      uade_terminal_mode = 0;
      break;
    case 'h':
      print_help();
      exit(0);
    case 'i':
      uade_ignore_player_check = 1;
      break;
    case 'j':
      uade_jump_pos = strtod(optarg, &endptr);
      if (*endptr != 0 || uade_jump_pos < 0.0) {
	fprintf(stderr, "uade123: illegal jump position: %s\n", optarg);
	exit(-1);
      }
      break;
    case 'k':
      uade_terminal_mode = 1;
      break;
    case 'K':
      uade_terminal_mode = 0;
      break;
    case 'm':
      playlist_add(&uade_playlist, optarg, 0);
      break;
    case 'n':
      uade_use_filter = 0;
      break;
    case 'p':
      config_set_panning(optarg);
      break;
    case 'P':
      GET_OPT_STRING(playername);
      playernamegiven = 1;
      have_modules = 1;
      break;
    case 'r':
      uade_recursivemode = 1;
      break;
    case 's':
      subsong = strtol(optarg, &endptr, 10);
      if (*endptr != 0 || subsong < 0 || subsong > 255) {
	fprintf(stderr, "uade123: illegal subsong string: %s\n", optarg);
	exit(-1);
      }
      break;
    case 'S':
      GET_OPT_STRING(scorename);
      break;
    case 't':
      config_set_timeout(optarg);
      break;
    case 'u':
      GET_OPT_STRING(uadename);
      break;
    case 'v':
      uade_verbose_mode = 1;
      break;
    case 'w':
      config_set_subsong_timeout(optarg);
      break;
    case 'y':
      config_set_silence_timeout(optarg);
      break;
    case 'z':
      playlist_random(&uade_playlist, 1);
      break;
    case '?':
    case ':':
      exit(-1);
    case OPT_FILTER:
      set_filter_on(optarg);
      break;
    case OPT_FORCE_LED:
      set_filter_on(NULL);
      uade_force_filter = 1;
      uade_filter_state = strtol(optarg, &endptr, 10);
      if (*endptr != 0 || uade_filter_state < 0 || uade_filter_state > 1) {
	fprintf(stderr, "uade123: illegal filter state: %s (must 0 or 1)\n", optarg);
	exit(-1);
      }
      break;
    case OPT_INTERPOLATOR:
      set_interpolation_mode(optarg);
      break;
    case OPT_STDERR:
      uade_terminal_file = stderr;
      break;
    case OPT_NO_SONG_END:
      uade_no_song_end = 1;
      break;
    default:
      fprintf(stderr, "impossible option\n");
      exit(-1);
    }
  }

  for (i = optind; i < argc; i++) {
    playlist_add(&uade_playlist, argv[i], uade_recursivemode);
    have_modules = 1;
  }

  if (have_modules == 0) {
    print_help();
    exit(0);
  }

  /* we want to control terminal differently in debug mode */
  if (debug_mode)
    uade_terminal_mode = 0;

  if (uade_terminal_mode)
    setup_terminal();

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
      strlcpy(uadename, UADE_CONFIG_UADE_CORE, sizeof(uadename));
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

  while (playlist_get_next(modulename, sizeof(modulename), &uade_playlist)) {
    char **playernames = NULL;
    int nplayers;
    ssize_t filesize;

    if (access(modulename, R_OK)) {
      fprintf(stderr, "can not read %s: %s\n", modulename, strerror(errno));
      goto nextsong;
    }

    nplayers = 1;
    if (playernamegiven == 0) {
      char *t, *tn;
      char *candidates;
      size_t len;

      debug("\n");

      candidates = fileformat_detection(modulename);

      if (candidates == NULL) {
	fprintf(stderr, "unknown format: %s\n", modulename);
	goto nextsong;
      }
      debug("player candidates: %s\n", candidates);

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

      if (nplayers > 1) {
	fprintf(stderr, "multiple players not supported yet\n");
	exit(-1);
      }

      if (nplayers < 1) {
	fprintf(stderr, "skipping file with unknown format: %s\n", modulename);
	goto nextsong;
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
	fprintf(stderr, "can not read %s: %s\n", playername, strerror(errno));
	goto nextsong;
      }
    }

    if ((filesize = stat_file_size(playername)) < 0) {
      fprintf(stderr, "can not stat player: %s\n", playername);
      goto nextsong;
    }
    if (uade_verbose_mode || modulename[0] == 0)
      fprintf(stderr, "Player: %s (%zd bytes)\n", playername, filesize);
    if (modulename[0] != 0) {
      if ((filesize = stat_file_size(modulename)) < 0) {
	fprintf(stderr, "can not stat module: %s\n", modulename);
	goto nextsong;
      }
      fprintf(stderr, "Module: %s (%zd bytes)\n", modulename, filesize);
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
      debug("uade refuses to play the song\n");
      if (uade_receive_short_message(UADE_COMMAND_TOKEN)) {
	fprintf(stderr, "uade123: can not receive token in main loop\n");
	exit(-1);
      }
      goto nextsong;
    }

    if (um->msgtype != UADE_REPLY_CAN_PLAY) {
      fprintf(stderr, "unexpected reply from uade: %d\n", um->msgtype);
      goto cleanup;
    }

    if (uade_receive_short_message(UADE_COMMAND_TOKEN) < 0) {
      fprintf(stderr, "uade123: can not receive token after play ack\n");
      goto cleanup;
    }

    if (uade_ignore_player_check) {
      if (uade_send_short_message(UADE_COMMAND_IGNORE_CHECK) < 0) {
	fprintf(stderr, "uade123: can not send ignore check message\n");
	exit(-1);
      }
    }

    if (uade_no_song_end) {
      if (uade_send_short_message(UADE_COMMAND_SONG_END_NOT_POSSIBLE) < 0) {
	fprintf(stderr, "uade123: can not send 'song end not possible'\n");
	exit(-1);
      }
    }

    if (subsong >= 0)
      set_subsong(um, subsong);

    send_filter_command();
    send_interpolation_command();

    if (!play_loop())
      goto cleanup;

  nextsong:

    if (playernames != NULL) {
      for (i = 0; i < nplayers; i++)
	free(playernames[i]);
      free(playernames);
    }
  }

  debug("killing child (%d)\n", uadepid);
  trivial_cleanup();
  return 0;

 cleanup:
  trivial_cleanup();
  return -1;
}


static void print_help(void)
{
  printf("uade123\n");
  printf(" by Heikki Orsila <heikki.orsila@iki.fi>\n");
  printf("    Michael Doering <mldoering@gmx.net>\n");
  printf("uadecore is based on the UAE source code. UAE is made by Bernd Schmidt et al.\n");
  printf("\n");
  printf("Usage: uade123 [<options>] <input file> ...\n");
  printf("\n");
  printf("Expert options:\n");
  printf(" -b dirname,         Set uade base directory (contains data files)\n");
  printf(" -c file,            Set uaerc config file name\n");
  printf(" -d/--debug,         Enable debug mode (expert only)\n");
  printf(" -S filename,        Set sound core name\n");
  printf(" -u uadename,        Set uadecore executable name\n");
  printf("\n");
  printf("Normal options:\n");
  printf(" -1, --one,          Play at most one subsong per file\n");
  printf(" -@ filename, --list filename,  Read playlist of files from 'filename'\n");
  printf(" -e format,          Set output file format. Use with -f. wav is the default\n");
  printf("                     format.\n");
  printf(" -f filename,        Write audio output into 'filename' (see -e also)\n");
  printf(" --filter,           Enable filter emulation. Default is ON.\n");
  printf(" --filter=a500/a1200 Enable A500 or A1200 filter model. Default is A1200.\n"); 
  printf(" --force-led=0/1,    Force LED state to 0 or 1. That is, filter is OFF or ON.\n");
  printf(" -g, --get-info,     Just print playername and subsong info on stdout.\n");
  printf("                     Do not play.\n");
  printf(" -h/--help,          Print help\n");
  printf(" -i, --ignore,       Ignore eagleplayer fileformat check result. Play always.\n");
  printf(" --interpolator=x    Set interpolator to x, where x = default, rh, crux or\n");
  printf("                     cspline.\n");
  printf(" -j x, --jump x,     Jump to time position 'x' seconds from the beginning.\n");
  printf("                     fractions of a second are allowed too.\n");
  printf(" -k, --keys,         Enable action keys for playback control on terminal\n");
  printf(" -K, --no-keys,      Disable action keys for playback control on terminal\n");
  printf(" -m filename,        Set module name\n");
  printf(" -n, --no-filter     No filter emulation.\n");
  printf(" --no-song-end,      Ignore song end report. Just keep playing.\n");
  printf(" -p x, --panning x,  Set panning value in range [0, 2] (0 = normal, 1 = mono)\n");
  printf(" -P filename,        Set player name\n");
  printf(" -r/--recursive,     Recursive directory scan\n");
  printf(" -s x, --subsong x,  Set subsong 'x'\n");
  printf(" --stderr,           Print messages on stderr.\n");
  printf(" -t x, --timeout x,  Set song timeout in seconds. -1 is infinite.\n");
  printf("                     Default is infinite.\n");
  printf(" -v,  --verbose,     Turn on verbose mode\n");
  printf(" -w, --subsong-timeout,  Set subsong timeout in seconds. -1 is infinite.\n");
  printf("                         Default is 512s\n");
  printf(" -y, --silence-timeout,  Set silence timeout in seconds. -1 is infinite.\n");
  printf("                         Default is 20s\n");
  printf(" -z, --shuffle,      Set shuffling mode for playlist\n");
  printf("\n");
  print_action_keys();
  printf("\n");
  printf("Example: Play all songs under /chip/fc directory in shuffling mode:\n");
  printf("  uade -z /chip/fc/*\n"); 
}


void print_action_keys(void)
{
  tprintf("Action keys for interactive mode:\n");
  tprintf(" '.'           Skip 10 seconds forward.\n");
  tprintf(" SPACE, 'b'    Go to next subsong.\n");
  tprintf(" 'c'           Pause.\n");
  tprintf(" 'f'           Toggle filter (takes filter control away from eagleplayer).\n");
  tprintf(" 'h'           Print this list.\n");
  tprintf(" RETURN, 'n'   Next subsong.\n");
  tprintf(" 'q'           Quit.\n");
  tprintf(" 's'           Toggle between shuffle mode and normal play.\n");
  tprintf(" 'x'           Restart current subsong.\n");
  tprintf(" 'z'           Previous subsong.\n");
}


void send_filter_command(void)
{
  struct uade_msg um = {.msgtype = UADE_COMMAND_FILTER, .size = 8};
  ((uint32_t *) um.data)[0] = htonl(uade_use_filter);
  if (uade_force_filter == 0) {
    ((uint32_t *) um.data)[1] = htonl(0);
  } else {
    ((uint32_t *) um.data)[1] = htonl(2 + (uade_filter_state & 1));
  }
  if (uade_send_message(&um)) {
    fprintf(stderr, "uade123: Can not setup filters.\n");
    exit(-1);
  }
}


static void send_interpolation_command(void)
{
  if (uade_interpolation_mode != NULL) {
    if (strlen(uade_interpolation_mode) == 0) {
      fprintf(stderr, "uade123: Interpolation mode may not be empty.\n");
      exit(-1);
    }
    if (uade_send_string(UADE_COMMAND_SET_INTERPOLATION_MODE, uade_interpolation_mode)) {
      fprintf(stderr, "uade123: Can not set interpolation mode.\n");
      exit(-1);
    }
  }
}


void set_filter_on(const char *model)
{
  if (uade_use_filter == 0)
    uade_use_filter = FILTER_MODEL_A1200;

  if (model == NULL)
    return;

  if (strcasecmp(model, "a500") == 0) {
    uade_use_filter = FILTER_MODEL_A500;
  } else if (strcasecmp(model, "a1200") == 0) {
    uade_use_filter = FILTER_MODEL_A1200;
  } else {
    fprintf(stderr, "Unknown filter model: %s\n", model);
    exit(-1);
  }
}


void set_interpolation_mode(const char *value)
{
  if (strlen(value) == 0) {
    fprintf(stderr, "Empty interpolator string not allowed.\n");
    exit(-1);
  }
  if ((uade_interpolation_mode = strdup(value)) == NULL) {
    fprintf(stderr, "No memory for interpolation mode.\n");
    exit(-1);
  }
}


static void set_subsong(struct uade_msg *um, int subsong)
{
  assert(subsong >= 0 && subsong < 256);
  *um = (struct uade_msg) {.msgtype = UADE_COMMAND_SET_SUBSONG, .size = 4};
  * (uint32_t *) um->data = htonl(subsong);
  if (uade_send_message(um) < 0) {
    fprintf(stderr, "could not set subsong\n");
    exit(-1);
  }
}


static void setup_sighandlers(void)
{
  struct sigaction act;
  memset(&act, 0, sizeof act);
  act.sa_handler = trivial_sigint;
  while (1) {
    if ((sigaction(SIGINT, &act, NULL)) < 0) {
      if (errno == EINTR)
	continue;
      fprintf(stderr, "can not install signal handler SIGINT: %s\n", strerror(errno));
      exit(-1);
    }
    break;
  }
  memset(&act, 0, sizeof act);
  act.sa_handler = trivial_sigchld;
  act.sa_flags = SA_NOCLDSTOP;
  while (1) {
    if ((sigaction(SIGCHLD, &act, NULL)) < 0) {
      if (errno == EINTR)
	continue;
      fprintf(stderr, "can not install signal handler SIGCHLD: %s\n", strerror(errno));
      exit(-1);
    }
    break;
  }
}


ssize_t stat_file_size(const char *name)
{
  struct stat st;
  if (stat(name, &st))
    return -1;
  return st.st_size;
}


/* test song_end_trigger by taking care of mutual exclusion with SIGINT */
int test_song_end_trigger(void)
{
  int ret;
  sigset_t set;
  if (sigemptyset(&set))
    goto sigerr;
  if (sigaddset(&set, SIGINT))
    goto sigerr;
  if (sigprocmask(SIG_BLOCK, &set, NULL))
    goto sigerr;
  ret = uade_song_end_trigger;
  uade_song_end_trigger = 0;
  if (sigprocmask(SIG_UNBLOCK, &set, NULL))
    goto sigerr;
  return ret;

 sigerr:
  fprintf(stderr, "signal hell\n");
  exit(-1);
}


static void trivial_cleanup(void)
{
  if (uadepid) {
    kill(uadepid, SIGTERM);
    uadepid = 0;
  }
  audio_close();
}


static void trivial_sigchld(int sig)
{
  pid_t process;
  int status;
  int successful;
  process = waitpid(-1, &status, WNOHANG);
  if (process == 0)
    return;
  if (uadepid == 0)
    return;
  if (process == uadepid) {
    successful = (WEXITSTATUS(status) == 0);
    debug("uade exited %ssuccessfully\n", successful == 1 ? "" : "un");
    uadepid = 0;
    uade_terminated = 1;
  }
}


static void trivial_sigint(int sig)
{
  static struct timeval otv = {.tv_sec = 0, .tv_usec = 0};
  struct timeval tv;
  int msecs;

  if (debug_mode == 1) {
    uade_debug_trigger = 1;
    return;
  }
  uade_song_end_trigger = 1;

  /* counts number of milliseconds between ctrl-c pushes, and terminates the
     prog if they are less than 100 msecs apart. */ 
  if (gettimeofday(&tv, 0)) {
    fprintf(stderr, "uade123: gettimeofday() does not work\n");
    return;
  }
  msecs = 0;
  if (otv.tv_sec) {
    msecs = (tv.tv_sec - otv.tv_sec) * 1000 + (tv.tv_usec - otv.tv_usec) / 1000;
    if (msecs < 100)
      exit(-1);
  }
  otv = tv;
}
