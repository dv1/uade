/* UADE2 plugin for XMMS
 *
 * Copyright (C) 2005  Heikki Orsila
 *                     heikki.orsila@iki.fi
 *                     http://zakalwe.virtuaalipalvelin.net/uade/
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) in any way you
 * want in your projects.
 */

#include <libgen.h>
#include <assert.h>

#include <netinet/in.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <uadeipc.h>
#include <eagleplayer.h>
#include <uadeconfig.h>
#include <uadecontrol.h>
#include <uadeconstants.h>
#include <strlrep.h>
#include <uadeconf.h>
#include <effects.h>
#include <postprocessing.h>

#include "plugin.h"
#include "subsongseek.h"


#define PLUGIN_DEBUG 1

#if PLUGIN_DEBUG
#define plugindebug(fmt, args...) do { fprintf(stderr, "%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ## args); } while(0)
#else
#define plugindebug(fmt, args...) 
#endif


static int initialize_song(char *filename);
static int test_silence(void *buf, size_t size);
static void uade_cleanup(void);
static void uade_get_song_info(char *filename, char **title, int *length);
static int uade_get_time(void);
static void uade_init(void);
static int uade_is_our_file(char *filename);
static void uade_pause(short paused);
static void uade_play_file(char *filename);
static void uade_seek(int time);
static void uade_stop(void);


/* GLOBAL VARIABLE DECLARATIONS */

static InputPlugin uade_ip = {
  .description = "UADE2 " UADE_VERSION,
  .init = uade_init,
  .is_our_file = uade_is_our_file,
  .play_file = uade_play_file,
  .stop = uade_stop,
  .pause = uade_pause,
  .seek = uade_seek,
  .get_time = uade_get_time,
  .cleanup = uade_cleanup,
  .get_song_info = uade_get_song_info
};

static const AFormat sample_format = FMT_S16_NE;

static int abort_playing;
static char configname[PATH_MAX];
static pthread_t decode_thread;
static char gui_filename[PATH_MAX];
static int gui_info_set;
static int ignore_player_check;
static int last_beat_played;
static int no_song_end;
static int one_subsong_per_file;
static int plugin_disabled;
static int silence_timeout;
static int subsong_timeout;
static int timeout;
static pid_t uadepid;

static time_t config_load_time;

int uade_cur_sub;
int uade_is_paused;
int uade_max_sub;
int uade_min_sub;
int uade_thread_running;
int uade_seek_forward;
int uade_select_sub;


static pthread_mutex_t vlock = PTHREAD_MUTEX_INITIALIZER;


static void set_defaults(void)
{
  ignore_player_check = 0;
  no_song_end = 0;
  one_subsong_per_file = 0;
  silence_timeout = 20;
  subsong_timeout = 512;
  timeout = -1;
  uade_use_panning = 0;
  uade_panning_value = 0.7;
  uade_use_postprocessing = 0;
  uade_use_headphones = 0;
  uade_postprocessing_setup(UADE_POSTPROCESSING_ENABLE);
}


static void load_config(void)
{
  struct stat st;
  struct uade_config uc;

  if (stat(configname, &st))
    return;

  if (st.st_mtime <= config_load_time)
    return;

  config_load_time = st.st_mtime;

  set_defaults();
  
  uade_load_config(&uc, configname);

  /*
  if (uc.filter)
  uade_use_filter = uc.filter;

  if (uc.force_filter_off) {
  uade_force_filter = 1;
  uade_filter_state = 0;
  }

  if (uc.no_filter)
  uade_use_filter = 0;
  */
  
  if (uc.headphones)
    uade_postprocessing_setup(UADE_HEADPHONES_ENABLE);

  if (uc.ignore_player_check)
    ignore_player_check = 1;

  /*
  if (uc.interpolator)
    uade_interpolation_mode = strdup(uc.interpolator);

  if (uc.no_filter)
      uade_use_filter = 0;
  */

  if (uc.one_subsong)
    one_subsong_per_file = 1;

  if (uc.panning != 0.0) {
    uade_panning_value = uc.panning;
    uade_postprocessing_setup(UADE_PANNING_ENABLE);
  }

  if (uc.silence_timeout)
    silence_timeout = uc.silence_timeout;

  if (uc.subsong_timeout)
    subsong_timeout = uc.subsong_timeout;

  if (uc.timeout)
    timeout = uc.timeout;
}


void uade_lock(void)
{
  if (pthread_mutex_lock(&vlock)) {
    fprintf(stderr, "UADE2 locking error.\n");
    exit(-1);
  }
}


void uade_unlock(void)
{
  if (pthread_mutex_unlock(&vlock)) {
    fprintf(stderr, "UADE2 unlocking error.\n");
    exit(-1);
  }
}


/* this function is first called by xmms. returns pointer to plugin table */
InputPlugin *get_iplugin_info(void)
{
  return &uade_ip;
}


/* xmms initializes uade by calling this function */
static void uade_init(void)
{
  struct stat st;

  set_defaults();

  /* If config exists in home, ignore global uade.conf. */
  snprintf(configname, sizeof configname, "%s/.uade2/uade.conf", getenv("HOME"));
  if (stat(configname, &st) == 0)
    return;

  /* No uade.conf in $HOME/.uade2/. */
  snprintf(configname, sizeof configname, "%s/uade.conf", UADE_CONFIG_BASE_DIR);
  if (stat(configname, &st) == 0)
    return;

  fprintf(stderr, "No config file found for UADE XMMS plugin. Will try to load config from\n");
  fprintf(stderr, "HOME/.uade2/uade.conf in the future.\n");
  snprintf(configname, sizeof configname, "%s/.uade2/uade.conf", getenv("HOME"));
}


static void uade_cleanup(void)
{
  if (uadepid) {
    kill(uadepid, SIGTERM);
  }
}


/* XMMS calls this function to check if filename belongs to this plugin. */
static int uade_is_our_file(char *filename)
{
  return uade_analyze_file_format(filename, UADE_CONFIG_BASE_DIR, 1) != NULL ? TRUE : FALSE;
}

static int initialize_song(char *filename)
{
  struct eagleplayer *ep;
  int ret;
  char modulename[PATH_MAX];
  char playername[PATH_MAX];
  char scorename[PATH_MAX];

  ep = uade_analyze_file_format(filename, UADE_CONFIG_BASE_DIR, 0);
  if (ep == NULL)
    return FALSE;

  strlcpy(modulename, filename, sizeof modulename);

  snprintf(scorename, sizeof scorename, "%s/score", UADE_CONFIG_BASE_DIR);

  if (strcmp(ep->playername, "custom") == 0) {
    strlcpy(playername, modulename, sizeof playername);
    modulename[0] = 0;
  } else {
    snprintf(playername, sizeof playername, "%s/players/%s", UADE_CONFIG_BASE_DIR, ep->playername);
  }

  ret = uade_song_initialization(scorename, playername, modulename);
  if (ret) {
    if (ret != UADECORE_CANT_PLAY && ret != UADECORE_INIT_ERROR) {
      fprintf(stderr, "Can not initialize song. Unknown error.\n");
      plugin_disabled = 1;
    }
    return FALSE;
  }

  if (ignore_player_check) {
    if (uade_send_short_message(UADE_COMMAND_IGNORE_CHECK) < 0) {
      fprintf(stderr, "Can not send ignore check message.\n");
      plugin_disabled = 1;
      return FALSE;
    }
  }

  if (no_song_end) {
    if (uade_send_short_message(UADE_COMMAND_SONG_END_NOT_POSSIBLE) < 0) {
      fprintf(stderr, "Can not send 'song end not possible'.\n");
      plugin_disabled = 1;
      return FALSE;
    }
  }

  return TRUE;
}

static void *play_loop(void *arg)
{
  enum uade_control_state state = UADE_S_STATE;
  int ret;
  int left = 0;
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;
  int subsong_end = 0;
  uint16_t *sm;
  int i;
  unsigned int play_bytes, tailbytes = 0;
  uint64_t subsong_bytes = 0, total_bytes = 0;
  char *reason;
  uint32_t *u32ptr;
  int writable;
  int framesize = UADE_CHANNELS * UADE_BYTES_PER_SAMPLE;
  int song_end_trigger = 0;
  int64_t skip_bytes = 0;

  while (1) {
    if (state == UADE_S_STATE) {

      assert(left == 0);

      if (abort_playing)
	break;

      uade_lock();
      if (uade_seek_forward) {
	skip_bytes += uade_seek_forward * UADE_BYTES_PER_SECOND;
	uade_seek_forward = 0;
      }
      if (uade_select_sub != -1) {
	uade_cur_sub = uade_select_sub;
	uade_change_subsong(uade_cur_sub);
	uade_ip.output->flush(0);
	uade_select_sub = -1;
	subsong_end = 0;
	subsong_bytes = 0;
	total_bytes = 0;
      }
      if (subsong_end && song_end_trigger == 0) {
	if (uade_cur_sub == -1 || uade_max_sub == -1) {
	  song_end_trigger = 1;
	} else {
	  uade_cur_sub++;
	  if (uade_cur_sub > uade_max_sub) {
	    song_end_trigger = 1;
	  } else {
	    uade_change_subsong(uade_cur_sub);
	    uade_ip.output->flush(0);
	    subsong_end = 0;
	    subsong_bytes = 0;
	  }
	}
      }
      uade_unlock();

      if (song_end_trigger) {
	/* We must drain the audio fast if abort_playing happens (e.g.
	   the user changes song when we are here waiting the sound device) */
	while (uade_ip.output->buffer_playing() && abort_playing == 0)

	  xmms_usleep(10000);
	break;
      }

      left = uade_read_request();
      
      if (uade_send_short_message(UADE_COMMAND_TOKEN)) {
	fprintf(stderr, "Can not send token.\n");
	return 0;
      }
      state = UADE_R_STATE;

    } else {

      if (uade_receive_message(um, sizeof(space)) <= 0) {
	fprintf(stderr, "Can not receive events from uade\n");
	exit(-1);
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

	if (subsong_end) {
	  play_bytes = tailbytes;
	  tailbytes = 0;
	} else {
	  play_bytes = um->size;
	}

	if (skip_bytes > 0) {
	  if (play_bytes <= skip_bytes) {
	    skip_bytes -= play_bytes;
	    play_bytes = 0;
	  } else {
	    play_bytes -= skip_bytes;
	    skip_bytes = 0;
	  }
	}

	subsong_bytes += play_bytes;
	total_bytes += play_bytes;

	while ((writable = uade_ip.output->buffer_free()) < play_bytes) {
	  if (abort_playing)
	    goto nowrite;
	  xmms_usleep(10000);
	}

	uade_effect_run((int16_t *) um->data, play_bytes / framesize);

	uade_ip.add_vis_pcm(uade_ip.output->written_time(), sample_format, UADE_CHANNELS, play_bytes, um->data);

	uade_ip.output->write_audio(um->data, play_bytes);

      nowrite:

	if (timeout != -1) {
	  if (song_end_trigger == 0) {
	    if (total_bytes / UADE_BYTES_PER_SECOND >= timeout) {
	      song_end_trigger = 1;
	    }
	  }
	}

	if (subsong_timeout != -1) {
	  if (subsong_end == 0 && song_end_trigger == 0) {
	    if (subsong_bytes / UADE_BYTES_PER_SECOND >= subsong_timeout) {
	      subsong_end = 1;
	    }
	  }
	}

	if (test_silence(um->data, play_bytes)) {
	  if (subsong_end == 0 && song_end_trigger == 0) {
	    subsong_end = 1;
	  }
	}

	assert (left >= um->size);
	left -= um->size;
	break;
	
      case UADE_REPLY_FORMATNAME:
	uade_check_fix_string(um, 128);
	/* plugindebug("Format name: %s\n", (uint8_t *) um->data); */
	break;

      case UADE_REPLY_MODULENAME:
	uade_check_fix_string(um, 128);
	/* plugindebug("Module name: %s\n", (uint8_t *) um->data); */
	break;

      case UADE_REPLY_MSG:
	uade_check_fix_string(um, 128);
	/* plugindebug("Message: %s\n", (char *) um->data); */
	break;

      case UADE_REPLY_PLAYERNAME:
	uade_check_fix_string(um, 128);
	/* plugindebug("Player name: %s\n", (uint8_t *) um->data); */
	break;

      case UADE_REPLY_SONG_END:
	if (um->size < 9) {
	  fprintf(stderr, "Invalid song end reply\n");
	  exit(-1);
	}
	tailbytes = ntohl(((uint32_t *) um->data)[0]);
	/* next ntohl() is only there for a principle. it is not useful */
	if (ntohl(((uint32_t *) um->data)[1]) == 0) {
	  /* normal happy song end. go to next subsong if any */
	  subsong_end = 1;
	} else {
	  /* unhappy song end (error in the 68k side). skip to next song
	     ignoring possible subsongs */
	  song_end_trigger = 1;
	}
	i = 0;
	reason = &((uint8_t *) um->data)[8];
	while (reason[i] && i < (um->size - 8))
	  i++;
	if (reason[i] != 0 || (i != (um->size - 9))) {
	  fprintf(stderr, "Broken reason string with song end notice\n");
	  exit(-1);
	}
	/* fprintf(stderr, "Song end (%s)\n", reason); */
	break;

      case UADE_REPLY_SUBSONG_INFO:
	if (um->size != 12) {
	  fprintf(stderr, "subsong info: too short a message\n");
	  exit(-1);
	}
	u32ptr = (uint32_t *) um->data;
	uade_lock();
	uade_min_sub = ntohl(u32ptr[0]);
	uade_max_sub = ntohl(u32ptr[1]);
	uade_cur_sub = ntohl(u32ptr[2]);

	if (!(-1 <= uade_min_sub && uade_min_sub <= uade_cur_sub && uade_cur_sub <= uade_max_sub)) {
	  int tempmin = uade_min_sub, tempmax = uade_max_sub;
	  fprintf(stderr, "uade: The player is broken. Subsong info does not match with %s.\n", gui_filename);
	  uade_min_sub = tempmin <= tempmax ? tempmin : tempmax;
	  uade_max_sub = tempmax >= tempmin ? tempmax : tempmin;
	  if (uade_cur_sub > uade_max_sub)
	    uade_max_sub = uade_cur_sub;
	  else if (uade_cur_sub < uade_min_sub)
	    uade_min_sub = uade_cur_sub;
	}
	uade_unlock();
	break;
	
      default:
	fprintf(stderr, "Expected sound data. got %d.\n", um->msgtype);
	plugin_disabled = 1;
	return NULL;
      }
    }
  }

  last_beat_played = 1;

  if (uade_send_short_message(UADE_COMMAND_REBOOT)) {
    fprintf(stderr, "Can not send reboot.\n");
    return 0;
  }

  if (uade_send_short_message(UADE_COMMAND_TOKEN)) {
    fprintf(stderr, "Can not send token.\n");
    return 0;
  }

  do {
    ret = uade_receive_message(um, sizeof(space));
    if (ret < 0) {
      fprintf(stderr, "Can not receive events from uade.\n");
      return NULL;
    }
    if (ret == 0) {
      fprintf(stderr, "End of input after reboot.\n");
      return NULL;
    }
  } while (um->msgtype != UADE_COMMAND_TOKEN);

  return NULL;
}


/* Note that this function has side effects (static int64_t silence_count) */
static int test_silence(void *buf, size_t size)
{
  int i, s, exceptioncounter;
  int16_t *sm;
  int nsamples;
  static int64_t silence_count = 0;

  if (silence_timeout < 0)
    return 0;

  exceptioncounter = 0;
  sm = buf;
  nsamples = size / 2;

  for (i = 0; i < nsamples; i++) {
    s = (sm[i] >= 0) ? sm[i] : -sm[i];
    if (s >= (32767 * 1 / 100)) {
      exceptioncounter++;
      if (exceptioncounter >= (size * 2 / 100)) {
	silence_count = 0;
	break;
      }
    }
  }
  if (i == nsamples) {
    silence_count += size;
    if (silence_count / UADE_BYTES_PER_SECOND >= silence_timeout) {
      silence_count = 0;
      return 1;
    }
  }
  return 0;
}


static void uade_play_file(char *filename)
{
  char tempname[PATH_MAX];
  char *t;

  load_config();

  uade_lock();
  abort_playing = 0;
  last_beat_played = 0;
  uade_cur_sub = uade_max_sub = uade_min_sub = -1;
  uade_is_paused = 0;
  uade_select_sub = -1;
  uade_seek_forward = 0;
  uade_unlock();

  strlcpy(tempname, filename, sizeof tempname);
  t = basename(tempname);
  if (t == NULL)
    t = filename;
  strlcpy(gui_filename, t, sizeof gui_filename);
  gui_info_set = 0;

  if (!uadepid) {
    char configname[PATH_MAX];
    snprintf(configname, sizeof configname, "%s/uaerc", UADE_CONFIG_BASE_DIR);
    uade_spawn(&uadepid, UADE_CONFIG_UADE_CORE, configname, 0);
  }

  if (!uade_ip.output->open_audio(sample_format, UADE_FREQUENCY, UADE_CHANNELS)) {
    abort_playing = 1;
    return;
  }

  if (plugin_disabled) {
    fprintf(stderr, "An error has occured. uade plugin is internally disabled.\n");
    goto err;
  }

  if (initialize_song(filename) == FALSE)
    goto err;

  uade_ip.set_info(gui_filename, -1, UADE_BYTES_PER_SECOND, UADE_FREQUENCY, UADE_CHANNELS);

  if (pthread_create(&decode_thread, 0, play_loop, 0)) {
    fprintf(stderr, "uade: can't create play_loop() thread\n");
    goto err;
  }

  uade_thread_running = 1;

  return;

 err:
  /* close audio that was opened */
  uade_ip.output->close_audio();
  abort_playing = 1;
}

static void uade_stop(void)
{
  abort_playing = 1;
  if (uade_thread_running) {
    pthread_join(decode_thread, 0);
    uade_thread_running = 0;
  }
  uade_ip.output->close_audio();
  uade_gui_close_subsong_win();
}


/* function that xmms calls when pausing or unpausing */
static void uade_pause(short paused)
{
  uade_lock();
  uade_is_paused = paused;
  uade_unlock();
  uade_ip.output->pause(paused);
}


static void uade_seek(int time)
{
  uade_gui_seek_subsong(time);
}


static int uade_get_time(void)
{
  if (abort_playing || last_beat_played)
    return -1;

  if (gui_info_set == 0 && uade_max_sub != -1) {
    uade_lock();
    if (uade_max_sub != -1) {
      /* Hack. Set info text late, because we didn't know subsong amounts
	 before this. Pass zero as a length so that the graphical
         play time counter will run but seek is still enabled. Passing -1
         would disable seeking. */
      uade_ip.set_info(gui_filename, 0, UADE_BYTES_PER_SECOND, UADE_FREQUENCY, UADE_CHANNELS);
    }
    uade_unlock();
    gui_info_set = 1;
  }

  return uade_ip.output->output_time();
}


static void uade_get_song_info(char *filename, char **title, int *length)
{
  char tempname[PATH_MAX];
  char *t;

  strlcpy(tempname, filename, sizeof tempname);
  t = basename(tempname);
  if (t == NULL)
    t = filename;
  if ((*title = strdup(t)) == NULL)
    plugindebug("Not enough memory for song info.\n");
  *length = -1;
}
