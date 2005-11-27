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

#include <assert.h>

#include <netinet/in.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <uadeipc.h>
#include <eagleplayer.h>
#include <uadeconfig.h>
#include <uadecontrol.h>
#include <uadeconstants.h>
#include <strlrep.h>

#include "plugin.h"
#include "subsongseek.h"


#define PLUGIN_DEBUG 1

#if PLUGIN_DEBUG
#define plugindebug(fmt, args...) do { fprintf(stderr, "%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ## args); } while(0)
#else
#define plugindebug(fmt, args...) 
#endif


static int initialize_song(char *filename);

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
  .description = "UADE2 " VERSION,
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
static pthread_t decode_thread;
static int plugin_disabled;
static int song_end_trigger;
static pid_t uadepid;
static int uade_ignore_player_check;
static int uade_no_song_end;

int uade_thread_running;

#if 0
static pthread_mutex_t vlock = PTHREAD_MUTEX_INITIALIZER;


static void lock_variables(void)
{
  if (pthread_mutex_lock(&vlock)) {
    fprintf(stderr, "UADE2 locking error.\n");
    exit(-1);
  }
}

static void unlock_variables(void)
{
  if (pthread_mutex_unlock(&vlock)) {
    fprintf(stderr, "UADE2 unlocking error.\n");
    exit(-1);
  }
}
#endif

/* this function is first called by xmms. returns pointer to plugin table */
InputPlugin *get_iplugin_info(void) {
  return &uade_ip;
}

/* xmms initializes uade by calling this function */
static void uade_init(void)
{
  plugindebug("\n");
}

static void uade_cleanup(void)
{
  plugindebug("\n");
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

  ep = uade_analyze_file_format(filename, UADE_CONFIG_BASE_DIR, 1);
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

  if (uade_ignore_player_check) {
    if (uade_send_short_message(UADE_COMMAND_IGNORE_CHECK) < 0) {
      fprintf(stderr, "Can not send ignore check message.\n");
      plugin_disabled = 1;
      return FALSE;
    }
  }

  if (uade_no_song_end) {
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
  int min_sub = -1, max_sub = -1, cur_sub = -1;
  uint16_t *sm;
  int i;
  int playbytes, tailbytes = 0;
  char *reason;
  uint32_t *u32ptr;
  int have_subsong_info = 0;
  int writable;

  while (1) {
    if (state == UADE_S_STATE) {

      assert(left == 0);

      if (abort_playing)
	break;

      if (subsong_end && song_end_trigger == 0) {
	if (cur_sub == -1 || max_sub == -1) {
	  song_end_trigger = 1;
	} else {
	  cur_sub++;
	  if (cur_sub > max_sub) {
	    song_end_trigger = 1;
	  } else {
	    uade_change_subsong(cur_sub);
	    subsong_end = 0;
	  }
	}
      }
      if (song_end_trigger)
	break;

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
	  playbytes = tailbytes;
	  tailbytes = 0;
	} else {
	  playbytes = um->size;
	}

	while ((writable = uade_ip.output->buffer_free()) < playbytes) {
	  if (abort_playing)
	    goto nowrite;
	  xmms_usleep(10000);
	}

	uade_ip.add_vis_pcm(uade_ip.output->written_time(), sample_format, UADE_CHANNELS, playbytes, um->data);

	uade_ip.output->write_audio(um->data, playbytes);

      nowrite:
	assert (left >= um->size);
	left -= um->size;
	break;
	
      case UADE_REPLY_FORMATNAME:
	uade_check_fix_string(um, 128);
	plugindebug("Format name: %s\n", (uint8_t *) um->data);
	break;

      case UADE_REPLY_MODULENAME:
	uade_check_fix_string(um, 128);
	plugindebug("Module name: %s\n", (uint8_t *) um->data);
	break;

      case UADE_REPLY_MSG:
	uade_check_fix_string(um, 128);
	plugindebug("Message: %s\n", (char *) um->data);
	break;

      case UADE_REPLY_PLAYERNAME:
	uade_check_fix_string(um, 128);
	plugindebug("Player name: %s\n", (uint8_t *) um->data);
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
	  abort_playing = 1;
	}
	i = 0;
	reason = &((uint8_t *) um->data)[8];
	while (reason[i] && i < (um->size - 8))
	  i++;
	if (reason[i] != 0 || (i != (um->size - 9))) {
	  fprintf(stderr, "Broken reason string with song end notice\n");
	  exit(-1);
	}
	fprintf(stderr, "Song end (%s)\n", reason);
	break;

      case UADE_REPLY_SUBSONG_INFO:
	if (um->size != 12) {
	  fprintf(stderr, "subsong info: too short a message\n");
	  exit(-1);
	}
	u32ptr = (uint32_t *) um->data;
	min_sub = ntohl(u32ptr[0]);
	max_sub = ntohl(u32ptr[1]);
	cur_sub = ntohl(u32ptr[2]);
	plugindebug("subsong: %d from range [%d, %d]\n", cur_sub, min_sub, max_sub);
	if (!(-1 <= min_sub && min_sub <= cur_sub && cur_sub <= max_sub)) {
	  int tempmin = min_sub, tempmax = max_sub;
	  fprintf(stderr, "The player is broken. Subsong info does not match.\n");
	  min_sub = tempmin <= tempmax ? tempmin : tempmax;
	  max_sub = tempmax >= tempmin ? tempmax : tempmin;
	  if (cur_sub > max_sub)
	    max_sub = cur_sub;
	  else if (cur_sub < min_sub)
	    min_sub = cur_sub;
	}
	if ((max_sub - min_sub) != 0)
	  fprintf(stderr, "There are %d subsongs in range [%d, %d].\n", 1 + max_sub - min_sub, min_sub, max_sub);
	have_subsong_info = 1;
	break;
	
      default:
	fprintf(stderr, "Expected sound data. got %d.\n", um->msgtype);
	plugin_disabled = 1;
	return NULL;
      }
    }
  }

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

  plugindebug("Play loop exitting successfully.\n");
  return NULL;
}


static void uade_play_file(char *filename)
{
  plugindebug("Play %s\n", filename);

  abort_playing = 0;
  song_end_trigger = 0;

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

  uade_ip.set_info(filename, -1, UADE_BYTES_PER_SECOND, UADE_FREQUENCY, UADE_CHANNELS);

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
  plugindebug("\n");
  abort_playing = 1;
  if (uade_thread_running) {
    pthread_join(decode_thread, 0);
    uade_thread_running = 0;
  }
  uade_ip.output->close_audio();
  uade_gui_close_subsong_win();
}


int uade_is_paused(void)
{
  plugindebug("\n");
  return FALSE;
}


/* function that xmms calls when pausing or unpausing */
static void uade_pause(short paused)
{
  plugindebug("Pause argument %d\n", paused);
  uade_ip.output->pause(paused);
}


static void uade_seek(int time)
{
  uade_gui_seek_subsong(time);
}


static int uade_get_time(void)
{
  if (abort_playing || song_end_trigger)
    return -1;

  return uade_ip.output->output_time();
}


static void uade_get_song_info(char *filename, char **title, int *length)
{
  plugindebug("\n");
  if ((*title = strdup(filename)) == NULL)
    plugindebug("Not enough memory for song info.\n");
  *length = -1;
}
