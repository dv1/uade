/* XMMS UADE plugin
 *
 * Copyright (C) 2000-2003  Heikki Orsila
 *                          heikki.orsila@iki.fi
 *                          http://uade.ton.tut.fi
 *
 * This plugin is based on xmms 0.9.6 wavplayer input plugin code. Since
 * then all code has been rewritten.
 *
 * Configure and About based on code of the null-plugin by Håvard Kvålen.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "plugin.h"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <uadeipc.h>
#include <eagleplayer.h>
#include <uadeconfig.h>
#include <uadecontrol.h>

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


static int abort_playing;
static pthread_t decode_thread;
static int thread_running;
static pid_t uadepid;


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
  char scorename[PATH_MAX];

  plugindebug("\n");

  ep = uade_analyze_file_format(filename, UADE_CONFIG_BASE_DIR, 1);
  if (ep == NULL)
    return FALSE;

  snprintf(scorename, sizeof scorename, "%s/score", UADE_CONFIG_BASE_DIR);

  ret = uade_song_initialization(scorename, ep->playername, filename);
  if (ret) {
    if (ret != UADECORE_CANT_PLAY && ret != UADECORE_INIT_ERROR)
      fprintf(stderr, "Can not initialize song. Unknown error.\n");
    return FALSE;
  }

  return TRUE;
}

static void *play_loop(void *arg)
{
  plugindebug("\n");
  if (abort_playing) {
    plugindebug("thread is aborted.\n");
    pthread_exit(0);
  }

  pthread_exit(0);
  return 0;
}


static void uade_play_file(char *filename)
{
  plugindebug("\n");

  abort_playing = 0;

  if (!uadepid) {
    char configname[PATH_MAX];

    plugindebug("spawning uadecore\n");
    uade_spawn(&uadepid, UADE_CONFIG_UADE_CORE, 0);
    plugindebug("uadecore spawned\n");

    snprintf(configname, sizeof configname, "%s/uaerc", UADE_CONFIG_BASE_DIR);
    if (uade_send_string(UADE_COMMAND_CONFIG, configname)) {
      fprintf(stderr, "Can not send config name.\n");
      abort_playing = 1;
      return;
    }
  }

  if (!uade_ip.output->open_audio(FMT_S16_NE, 44100, 2)) {
    abort_playing = 1;
    return;
  }

  if (initialize_song(filename) == FALSE) {
    /* This will cause uade_get_time() to return -1 for XMMS to notify
       that playing should be stopped. */
    abort_playing = 1;
    return;
  }

  if (pthread_create(&decode_thread, 0, play_loop, 0)) {
    fprintf(stderr, "uade: can't create play_loop() thread\n");
    goto err;
  }
  thread_running = 1;

  return;

 err:
  /* close audio that was opened */
  uade_ip.output->close_audio();
}

static void uade_stop(void)
{
  plugindebug("\n");
  if (thread_running) {
    pthread_join(decode_thread, 0);
    thread_running = 0;
  }
  uade_ip.output->close_audio();
}


int uade_is_paused(void)
{
  plugindebug("\n");
  return FALSE;
}


/* function that xmms calls when pausing or unpausing */
static void uade_pause(short paused)
{
  plugindebug("\n");
  uade_ip.output->pause(paused);
}


static void uade_seek(int time)
{
  plugindebug("\n");
}


static int uade_get_time(void)
{
  if (abort_playing)
    return -1;
  return 0;
}


static void uade_get_song_info(char *filename, char **title, int *length)
{
  plugindebug("\n");
  if ((*title = strdup(filename)) == NULL)
    plugindebug("Not enough memory for song info.\n");
  *length = -1;
}
