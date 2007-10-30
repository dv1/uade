/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005-2006 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
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

#include "uadecontrol.h"
#include "uadeipc.h"
#include "strlrep.h"
#include "uadeconfig.h"
#include "eagleplayer.h"
#include "uadeconf.h"
#include "sysincludes.h"
#include "songdb.h"


#include "uade123.h"
#include "playlist.h"
#include "playloop.h"
#include "audio.h"
#include "terminal.h"
#include "amigafilter.h"

int uade_debug_trigger;
int uade_info_mode;
double uade_jump_pos = 0.0;

int uade_no_audio_output;
int uade_no_text_output;

char uade_output_file_format[16];
char uade_output_file_name[PATH_MAX];
struct playlist uade_playlist;
FILE *uade_terminal_file;
int uade_terminated;
int uade_song_end_trigger;

static int debug_mode;
static char md5name[PATH_MAX];
static time_t md5_load_time;
static pid_t uadepid = -1;
static char uadename[PATH_MAX];


static void print_help(void);
static void setup_sighandlers(void);
ssize_t stat_file_size(const char *name);
static void trivial_sigchld(int sig);
static void trivial_sigint(int sig);
static void cleanup(void);


static void load_content_db(struct uade_config *uc)
{
  struct stat st;
  time_t curtime = time(NULL);
  char name[PATH_MAX];

  if (curtime)
    md5_load_time = curtime;

  if (md5name[0] == 0) {
    char *home = uade_open_create_home();
    if (home)
      snprintf(md5name, sizeof md5name, "%s/.uade2/contentdb", home);
  }

  /* First try to read users database */
  if (md5name[0]) {
    /* Try home directory first */
    if (stat(md5name, &st) == 0) {
      if (uade_read_content_db(md5name))
	return;
    } else {
      FILE *f = fopen(md5name, "w");
      if (f)
	fclose(f);
      uade_read_content_db(md5name);
    }
  }

  /* Second try to read global database, this does not override any data
     from user database */
  snprintf(name, sizeof name, "%s/contentdb", uc->basedir.name);
  if (stat(name, &st) == 0)
    uade_read_content_db(name);
}


static void save_content_db(void)
{
  struct stat st;
  if (md5name[0] && stat(md5name, &st) == 0) {

    if (md5_load_time < st.st_mtime)
      uade_read_content_db(md5name);

    uade_save_content_db(md5name);
  }
}


int main(int argc, char *argv[])
{
  int i;
  char configname[PATH_MAX] = "";
  char playername[PATH_MAX] = "";
  char scorename[PATH_MAX] = "";
  int playernamegiven = 0;
  char tmpstr[PATH_MAX + 256];
  long subsong = -1;
  int have_modules = 0;
  int ret;
  char *endptr;
  int uadeconf_loaded, songconf_loaded;
  char songconfname[PATH_MAX] = "";
  char uadeconfname[PATH_MAX];
  struct uade_effect effects;
  struct uade_config uc, uc_cmdline, uc_main;
  struct uade_ipc uadeipc;
  char songoptions[256] = "";
  int have_song_options = 0;
  int plistdir;

  enum {
    OPT_BASEDIR = 0x2000,
    OPT_REPEAT,
    OPT_SCOPE,
    OPT_SET,
    OPT_STDERR,
    OPT_VERSION
  };

  struct option long_options[] = {
    {"basedir",          1, NULL, OPT_BASEDIR},
    {"buffer-time",      1, NULL, UC_BUFFER_TIME},
    {"debug",            0, NULL, 'd'},
    {"detect-format-by-content", 0, NULL, UC_CONTENT_DETECTION},
    {"disable-timeouts", 0, NULL, UC_DISABLE_TIMEOUTS},
    {"enable-timeouts",  0, NULL, UC_ENABLE_TIMEOUTS},
    {"ep-option",        1, NULL, 'x'},
    {"filter",           2, NULL, UC_FILTER_TYPE},
    {"force-led",        1, NULL, UC_FORCE_LED},
    {"frequency",        1, NULL, UC_FREQUENCY},
    {"gain",             1, NULL, 'G'},
    {"get-info",         0, NULL, 'g'},
    {"headphones",       0, NULL, UC_HEADPHONES},
    {"headphones2",      0, NULL, UC_HEADPHONES2},
    {"help",             0, NULL, 'h'},
    {"ignore",           0, NULL, 'i'},
    {"interpolator",     1, NULL, UC_RESAMPLER},
    {"jump",             1, NULL, 'j'},
    {"keys",             1, NULL, 'k'},
    {"list",             1, NULL, '@'},
    {"magic",            0, NULL, UC_CONTENT_DETECTION},
    {"no-ep-end-detect", 0, NULL, 'n'},
    {"no-song-end",      0, NULL, 'n'},
    {"normalise",        2, NULL, UC_NORMALISE},
    {"ntsc",             0, NULL, UC_NTSC},
    {"one",              0, NULL, '1'},
    {"pal",              0, NULL, UC_PAL},
    {"panning",          1, NULL, 'p'},
    {"recursive",        0, NULL, 'r'},
    {"repeat",           0, NULL, OPT_REPEAT},
    {"resampler",        1, NULL, UC_RESAMPLER},
    {"scope",            0, NULL, OPT_SCOPE},
    {"shuffle",          0, NULL, 'z'},
    {"set",              1, NULL, OPT_SET},
    {"silence-timeout",  1, NULL, 'y'},
    {"speed-hack",       0, NULL, UC_SPEED_HACK},
    {"stderr",           0, NULL, OPT_STDERR},
    {"subsong",          1, NULL, 's'},
    {"subsong-timeout",  1, NULL, 'w'},
    {"timeout",          1, NULL, 't'},
    {"verbose",          0, NULL, 'v'},
    {"version",          0, NULL, OPT_VERSION},
    {NULL,               0, NULL, 0}
  };

  uade_config_set_defaults(&uc_cmdline);

  if (!playlist_init(&uade_playlist)) {
    fprintf(stderr, "Can not initialize playlist.\n");
    exit(1);
  }

#define GET_OPT_STRING(x) if (strlcpy((x), optarg, sizeof(x)) >= sizeof(x)) {\
	fprintf(stderr, "Too long a string for option %c.\n", ret); \
         exit(1); \
      }

  while ((ret = getopt_long(argc, argv, "@:1de:f:gG:hij:k:m:np:P:rs:S:t:u:vw:x:y:z", long_options, 0)) != -1) {
    switch (ret) {
    case '@':
      do {
	FILE *listfile = fopen(optarg, "r");
	if (listfile == NULL) {
	  fprintf(stderr, "Can not open list file: %s\n", optarg);
	  exit(1);
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
      uade_set_config_option(&uc_cmdline, UC_ONE_SUBSONG, NULL);
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
      uade_no_audio_output = 1;
      uade_no_text_output = 1;
      uade_set_config_option(&uc_cmdline, UC_ACTION_KEYS, "off");
      break;
    case 'G':
      uade_set_config_option(&uc_cmdline, UC_GAIN, optarg);
      break;
    case 'h':
      print_help();
      exit(0);
    case 'i':
      uade_set_config_option(&uc_cmdline, UC_IGNORE_PLAYER_CHECK, NULL);
      break;
    case 'j':
      uade_jump_pos = strtod(optarg, &endptr);
      if (*endptr != 0 || uade_jump_pos < 0.0) {
	fprintf(stderr, "Invalid jump position: %s\n", optarg);
	exit(1);
      }
      break;
    case 'k':
      uade_set_config_option(&uc_cmdline, UC_ACTION_KEYS, optarg);
      break;
    case 'm':
      playlist_add(&uade_playlist, optarg, 0);
      have_modules = 1;
      break;
    case 'n':
      uade_set_config_option(&uc_cmdline, UC_NO_EP_END, NULL);
      break;
    case 'p':
      uade_set_config_option(&uc_cmdline, UC_PANNING_VALUE, optarg);
      break;
    case 'P':
      GET_OPT_STRING(playername);
      playernamegiven = 1;
      have_modules = 1;
      break;
    case 'r':
      uade_set_config_option(&uc_cmdline, UC_RECURSIVE_MODE, NULL);
      break;
    case 's':
      subsong = strtol(optarg, &endptr, 10);
      if (*endptr != 0 || subsong < 0 || subsong > 255) {
	fprintf(stderr, "Invalid subsong string: %s\n", optarg);
	exit(1);
      }
      break;
    case 'S':
      GET_OPT_STRING(scorename);
      break;
    case 't':
      uade_set_config_option(&uc_cmdline, UC_TIMEOUT_VALUE, optarg);
      break;
    case 'u':
      GET_OPT_STRING(uadename);
      break;
    case 'v':
      uade_set_config_option(&uc_cmdline, UC_VERBOSE, NULL);
      break;
    case 'w':
      uade_set_config_option(&uc_cmdline, UC_SUBSONG_TIMEOUT_VALUE, optarg);
      break;
    case 'x':
      uade_set_config_option(&uc_cmdline, UC_EAGLEPLAYER_OPTION, optarg);
      break;
    case 'y':
      uade_set_config_option(&uc_cmdline, UC_SILENCE_TIMEOUT_VALUE, optarg);
      break;
    case 'z':
      uade_set_config_option(&uc_cmdline, UC_RANDOM_PLAY, NULL);
      break;
    case '?':
    case ':':
      exit(1);

    case OPT_BASEDIR:
      uade_set_config_option(&uc_cmdline, UC_BASE_DIR, optarg);
      break;

    case OPT_REPEAT:
      playlist_repeat(&uade_playlist);
      break;

    case OPT_SCOPE:
      uade_no_text_output = 1;
      uade_set_config_option(&uc_cmdline, UC_USE_TEXT_SCOPE, NULL);
      break;

    case OPT_SET:
      have_song_options = 1;
      strlcpy(songoptions, optarg, sizeof songoptions);
      break;

    case OPT_STDERR:
      uade_terminal_file = stderr;
      break;

    case OPT_VERSION:
      printf("uade123 %s\n", UADE_VERSION);
      exit(0);
      break;

    case UC_BUFFER_TIME:
    case UC_FILTER_TYPE:
    case UC_FORCE_LED:
    case UC_FREQUENCY:
    case UC_NORMALISE:
    case UC_RESAMPLER:
      uade_set_config_option(&uc_cmdline, ret, optarg);
      break;

    case UC_CONTENT_DETECTION:
    case UC_DISABLE_TIMEOUTS:
    case UC_ENABLE_TIMEOUTS:
    case UC_HEADPHONES:
    case UC_HEADPHONES2:
    case UC_NTSC:
    case UC_PAL:
    case UC_SPEED_HACK:
      uade_set_config_option(&uc_cmdline, ret, NULL);
      break;

    default:
      fprintf(stderr, "Impossible option.\n");
      exit(1);
    }
  }

  uadeconf_loaded = uade_load_initial_config(uadeconfname, sizeof uadeconfname,
					     &uc_main, &uc_cmdline);

  /* Merge loaded configurations and command line options */
  uc = uc_main;
  uade_merge_configs(&uc, &uc_cmdline);

  if (uadeconf_loaded == 0) {
    debug(uc.verbose, "Not able to load uade.conf from ~/.uade2/ or %s/.\n", uc.basedir.name);
  } else {
    debug(uc.verbose, "Loaded configuration: %s\n", uadeconfname);
  }

  songconf_loaded = uade_load_initial_song_conf(songconfname,
						sizeof songconfname,
						&uc_main, &uc_cmdline);

  if (songconf_loaded == 0) {
    debug(uc.verbose, "Not able to load song.conf from ~/.uade2/ or %s/.\n", uc.basedir.name);
  } else {
    debug(uc.verbose, "Loaded song.conf: %s\n", songconfname);
  }

  load_content_db(&uc);

  for (i = optind; i < argc; i++) {
    /* Play files */
    playlist_add(&uade_playlist, argv[i], uc.recursive_mode);
    have_modules = 1;
  }

  if (have_song_options) {
    char homesongconfname[PATH_MAX];
    struct playlist_iterator pli;
    char *songfile;
    char *home;

    home = uade_open_create_home();
    /* Make song.conf settings */
    if (home == NULL) {
      fprintf(stderr, "No $HOME for song.conf :(\n");
      exit(1);
    }

    snprintf(homesongconfname, sizeof homesongconfname, "%s/.uade2/song.conf",
	     home);

    if (songconf_loaded == 0)
      strlcpy(songconfname, homesongconfname, sizeof songconfname);

    playlist_iterator(&pli, &uade_playlist);

    while (1) {
      songfile = playlist_iterator_get(&pli);
      if (songfile == NULL)
	break;

      if (uade_update_song_conf(songconfname, homesongconfname,
				songfile, songoptions) == 0) {
	fprintf(stderr, "Could not update song.conf entry for %s\n", argv[i]);
	break;
      }
    }

    exit(0);
  }

  if (uc.random_play)
    playlist_randomize(&uade_playlist);

  if (have_modules == 0) {
    print_help();
    exit(0);
  }

  /* we want to control terminal differently in debug mode */
  if (debug_mode)
    uc.action_keys = 0;

  if (uc.action_keys)
    setup_terminal();

  do {
    DIR *bd;
    if ((bd = opendir(uc.basedir.name)) == NULL) {
      fprintf(stderr, "Could not access dir %s: %s\n", uc.basedir.name, strerror(errno));
      exit(1);
    }
    closedir(bd);

    snprintf(configname, sizeof configname, "%s/uaerc", uc.basedir.name);

    if (scorename[0] == 0)
      snprintf(scorename, sizeof scorename, "%s/score", uc.basedir.name);

    if (uadename[0] == 0)
      strlcpy(uadename, UADE_CONFIG_UADE_CORE, sizeof uadename);

    if (access(configname, R_OK)) {
      fprintf(stderr, "Could not read %s: %s\n", configname, strerror(errno));
      exit(1);
    }
    if (access(scorename, R_OK)) {
      fprintf(stderr, "Could not read %s: %s\n", scorename, strerror(errno));
      exit(1);
    }
    if (access(uadename, X_OK)) {
      fprintf(stderr, "Could not execute %s: %s\n", uadename, strerror(errno));
      exit(1);
    }
  } while (0);

  setup_sighandlers();

  uade_spawn(&uadeipc, &uadepid, uadename, configname);

  if (!audio_init(uc.frequency, uc.buffer_time))
    goto cleanup;

  plistdir = 0;

  while (1) {
    ssize_t filesize;
    struct uade_song *us;
    /* modulename and songname are a bit different. modulename is the name
       of the song from uadecore's point of view and songname is the
       name of the song from user point of view. Sound core considers all
       custom songs to be players (instead of modules) and therefore modulename
       will become a zero-string with custom songs. */
    char modulename[PATH_MAX];
    char songname[PATH_MAX];
    struct eagleplayer *ep = NULL;

    if (!playlist_get(modulename, sizeof modulename, &uade_playlist, plistdir))
      break;

    plistdir = 1;

    uc = uc_main;

    if (uc_cmdline.verbose)
      uc.verbose = 1;

    if (playernamegiven == 0) {
      debug(uc.verbose, "\n");

      ep = uade_analyze_file_format(modulename, &uc);
      if (ep == NULL) {
	fprintf(stderr, "Unknown format: %s\n", modulename);
	continue;
      }

      debug(uc.verbose, "Player candidate: %s\n", ep->playername);

      if (strcmp(ep->playername, "custom") == 0) {
	strlcpy(playername, modulename, sizeof playername);
	modulename[0] = 0;
      } else {
	snprintf(playername, sizeof playername, "%s/players/%s", uc_cmdline.basedir.name, ep->playername);
      }
    }

    if (strlen(playername) == 0) {
      fprintf(stderr, "Error: an empty player name given\n");
      goto cleanup;
    }

    /* If no modulename given, try the playername as it can be a custom song */
    strlcpy(songname, modulename[0] ? modulename : playername, sizeof songname);

    if ((us = uade_alloc_song(songname)) == NULL) {
      fprintf(stderr, "Can not read %s: %s\n", songname, strerror(errno));
      continue;
    }

    /* The order of parameter processing is important:
     * 0. set uade.conf options (done before this)
     * 1. set eagleplayer attributes
     * 2. set song attributes
     * 3. set command line options
     */

    if (ep != NULL)
      uade_set_ep_attributes(&uc, us, ep);

    if (uade_set_song_attributes(&uc, playername, sizeof playername, us)) {
      debug(uc.verbose, "Song rejected based on attributes: %s\n",
	    us->module_filename);
      uade_unalloc_song(us);
      continue;
    }

    uade_merge_configs(&uc, &uc_cmdline);

    /* Now we have the final configuration in "uc". */

    uade_set_effects(&effects, &uc);

    if ((filesize = stat_file_size(playername)) < 0) {
      fprintf(stderr, "Can not find player: %s (%s)\n", playername, strerror(errno));
      uade_unalloc_song(us);
      continue;
    }

    debug(uc.verbose, "Player: %s (%zd bytes)\n", playername, filesize);

    fprintf(stderr, "Song: %s (%zd bytes)\n", us->module_filename, us->bufsize);

    if ((ret = uade_song_initialization(scorename, playername, modulename, us, &uadeipc, &uc))) {
      if (ret == UADECORE_INIT_ERROR) {
	uade_unalloc_song(us);
	goto cleanup;
      } else if (ret == UADECORE_CANT_PLAY) {
	debug(uc.verbose, "Uadecore refuses to play the song.\n");
	uade_unalloc_song(us);
	continue;
      }
      fprintf(stderr, "Unknown error from uade_song_initialization()\n");
      exit(1);
    }

    if (subsong >= 0)
      uade_set_subsong(subsong, &uadeipc);

    plistdir = play_loop(&uadeipc, us, &effects, &uc);

    uade_unalloc_song(us);

    if (plistdir == 0)
      goto cleanup;
  }

  debug(uc_cmdline.verbose || uc_main.verbose, "Killing child (%d).\n", uadepid);
  cleanup();
  return 0;

 cleanup:
  cleanup();
  return 1;
}


static void print_help(void)
{
  printf("uade123 %s\n", UADE_VERSION);
  printf(" by Heikki Orsila <heikki.orsila@iki.fi>\n");
  printf("    Michael Doering <mldoering@gmx.net>\n");
  printf("uadecore is based on the UAE source code. UAE is made by Bernd Schmidt et al.\n");
  printf("\n");
  printf("Usage: uade123 [<options>] <input file> ...\n");
  printf("\n");
  printf("Expert options:\n");
  printf(" --basedir=dirname,  Set uade base directory (contains data files)\n");
  printf(" -d, --debug,        Enable debug mode (expert only)\n");
  printf(" -S filename,        Set sound core name\n");
  printf(" --scope             Turn on Paula hardware register debug mode\n");
  printf(" -u uadename,        Set uadecore executable name\n");
  printf("\n");
  printf("Normal options:\n");
  printf(" -1, --one,          Play at most one subsong per file\n");
  printf(" -@ filename, --list=filename,  Read playlist of files from 'filename'\n");
  printf(" --buffer-time=x,    Set audio buffer length to x milliseconds. The default\n");
  printf("                     value is determined by the libao.\n");
  printf(" --detect-format-by-content, Detect modules strictly by file content.\n");
  printf("                     Detection will ignore file name prefixes.\n");
  printf(" --disable-timeouts, Disable timeouts. This can be used for songs that are\n");
  printf("                     known to end. Useful for recording fixed time pieces.\n");
  printf("                     Some formats, such as protracker, disable timeouts\n");
  printf("                     automatically, because it is known they will always end.\n");
  printf(" -e format,          Set output file format. Use with -f. wav is the default\n");
  printf("                     format.\n");
  printf(" --enable-timeouts,  Enable timeouts. See --disable-timeouts.\n");
  printf(" -f filename,        Write audio output into 'filename' (see -e also)\n");
  printf(" --filter=model      Set filter model to A500, A1200 or NONE. The default is\n");
  printf("                     A500. NONE means disabling the filter.\n");
  printf(" --filter,           Enable filter emulation. It is enabled by default.\n");
  printf(" --force-led=0/1,    Force LED state to 0 or 1. That is, filter is OFF or ON.\n");
  printf(" --frequency=x,      Set output frequency to x Hz. The default is 44,1 kHz.\n");
  printf(" -G x, --gain=x,     Set volume gain to x in range [0, 128]. Default is 1,0.\n");
  printf(" -g, --get-info,     Just print playername and subsong info on stdout.\n");
  printf("                     Do not play.\n");
  printf(" -h, --help,         Print help\n");
  printf(" --headphones,       Enable headphones postprocessing effect.\n");
  printf(" --headphones2       Enable headphones 2 postprocessing effect.\n");
  printf(" -i, --ignore,       Ignore eagleplayer fileformat check result. Play always.\n");
  printf(" -j x, --jump=x,     Jump to time position 'x' seconds from the beginning.\n");
  printf("                     fractions of a second are allowed too.\n");
  printf(" -k 0/1, --keys=0/1, Turn action keys on (1) or off (0) for playback control\n");
  printf("                     on terminal. \n");
  printf(" -m filename,        Set module name\n");
  printf(" -n, --no-ep-end-detect, Ignore song end reported by the eagleplayer. Just\n");
  printf("                     keep playing. This does not affect timeouts. Check -w.\n");
  printf(" --ntsc,             Set NTSC mode for playing (can be buggy).\n");
  printf(" --pal,              Set PAL mode (default)\n");
  printf(" --normalise,        Enable normalise postprocessing effect.\n");
  printf(" -p x, --panning=x,  Set panning value in range [0, 2]. 0 is full stereo,\n");
  printf("                     1 is mono, and 2 is inverse stereo. The default is 0,7.\n");
  printf(" -P filename,        Set player name\n");
  printf(" -r, --recursive,    Recursive directory scan\n");
  printf(" --repeat,           Play playlist over and over again\n");
  printf(" --resampler=x       Set resampling method to x, where x = default, sinc\n");
  printf("                     or none.\n");
  printf(" -s x, --subsong=x,  Set subsong 'x'\n");
  printf(" --set=\"options\"     Set song.conf options for each given song.\n");
  printf(" --speed-hack,       Set speed hack on. This gives more virtual CPU power.\n");
  printf(" --stderr,           Print messages on stderr.\n");
  printf(" -t x, --timeout=x,  Set song timeout in seconds. -1 is infinite.\n");
  printf("                     Default is infinite.\n");
  printf(" -v,  --verbose,     Turn on verbose mode\n");
  printf(" -w x, --subsong-timeout=x,  Set subsong timeout in seconds. -1 is infinite.\n");
  printf("                             Default is 512s\n");
  printf(" -x y, --ep-option=y, Use eagleplayer option y. Option can be used many times.\n");
  printf("                      Example: uade123 -x type:nt10 mod.foobar, will play\n");
  printf("                      mod.foobar as a Noisetracker 1.0 module. See eagleplayer\n");
  printf("                      options from the man page.\n");
  printf(" -y x, --silence-timeout=x,  Set silence timeout in seconds. -1 is infinite.\n");
  printf("                         Default is 20s\n");
  printf(" -z, --shuffle,      Randomize playlist order before playing.\n");
  printf("\n");
  print_action_keys();
  printf("\n");
  printf("Example: Play all songs under /chip/fc directory in shuffling mode:\n");
  printf("  uade -z /chip/fc/*\n"); 
}


void print_action_keys(void)
{
  tprintf("Action keys for interactive mode:\n");
  tprintf(" [0-9]         Change subsong.\n");
  tprintf(" '<'           Previous song.\n");
  tprintf(" '.'           Skip 10 seconds forward.\n");
  tprintf(" SPACE, 'b'    Next subsong.\n");
  tprintf(" 'c'           Pause.\n");
  tprintf(" 'f'           Toggle filter (takes filter control away from eagleplayer).\n");
  tprintf(" 'g'           Toggle gain effect.\n");
  tprintf(" 'h'           Print this list.\n");
  tprintf(" 'H'           Toggle headphones effect.\n");
  tprintf(" 'i'           Print module info.\n");
  tprintf(" 'I'           Print hex dump of head of module.\n");
  tprintf(" 'N'           Toggle normalise effect.\n");
  tprintf(" RETURN, 'n'   Next song.\n");
  tprintf(" 'p'           Toggle postprocessing effects.\n");
  tprintf(" 'P'           Toggle panning effect. Default value is 0.7.\n");
  tprintf(" 'q'           Quit.\n");
  tprintf(" 's'           Toggle between random and normal play.\n");
  tprintf(" 'v'           Toggle verbose mode.\n");
  tprintf(" 'x'           Restart current subsong.\n");
  tprintf(" 'z'           Previous subsong.\n");
}


static void setup_sighandlers(void)
{
  struct sigaction act;

  memset(&act, 0, sizeof act);
  act.sa_handler = trivial_sigint;

  if ((sigaction(SIGINT, &act, NULL)) < 0) {
    fprintf(stderr, "can not install signal handler SIGINT: %s\n", strerror(errno));
    exit(1);
  }

  memset(&act, 0, sizeof act);
  act.sa_handler = trivial_sigchld;
  act.sa_flags = SA_NOCLDSTOP;

  if ((sigaction(SIGCHLD, &act, NULL)) < 0) {
    fprintf(stderr, "can not install signal handler SIGCHLD: %s\n", strerror(errno));
    exit(1);
  }
}


ssize_t stat_file_size(const char *name)
{
  struct stat st;

  if (stat(name, &st))
    return -1;

  return st.st_size;
}


/* test song_end_trigger by taking care of mutual exclusion with signal
   handlers */
int test_song_end_trigger(void)
{
  int ret;
  sigset_t set;

  /* Block SIGINT while handling uade_song_end_trigger */
  if (sigemptyset(&set))
    goto sigerr;
  if (sigaddset(&set, SIGINT))
    goto sigerr;
  if (sigprocmask(SIG_BLOCK, &set, NULL))
    goto sigerr;

  ret = uade_song_end_trigger;
  uade_song_end_trigger = 0;

  /* Unblock SIGINT */
  if (sigprocmask(SIG_UNBLOCK, &set, NULL))
    goto sigerr;

  return ret;

 sigerr:
  fprintf(stderr, "signal hell\n");
  exit(1);
}


static void cleanup(void)
{
  save_content_db();

  if (uadepid != -1) {
    kill(uadepid, SIGTERM);
    uadepid = -1;
  }

  audio_close();
}


static void trivial_sigchld(int sig)
{
  int status;

  if (waitpid(-1, &status, WNOHANG) == uadepid) {
    uadepid = -1;
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
  if (gettimeofday(&tv, NULL)) {
    fprintf(stderr, "Gettimeofday() does not work.\n");
    return;
  }

  msecs = 0;
  if (otv.tv_sec) {
    msecs = (tv.tv_sec - otv.tv_sec) * 1000 + (tv.tv_usec - otv.tv_usec) / 1000;
    if (msecs < 100)
      exit(1);
  }

  otv = tv;
}
