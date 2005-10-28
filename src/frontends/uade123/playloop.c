/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <uadecontrol.h>

#include "uade123.h"
#include "effects.h"
#include "audio.h"
#include "terminal.h"
#include "playlist.h"


static int uade_test_silence(void *buf, size_t size)
{
  int i, s, exceptioncounter;
  int16_t *sm;
  static int64_t zero_count = 0;
  int nsamples;

  if (uade_silence_timeout < 0)
    return 0;

  exceptioncounter = 0;
  sm = buf;
  nsamples = size / 2;

  for (i = 0; i < nsamples; i++) {
    s = (sm[i] >= 0) ? sm[i] : -sm[i];
    if (s >= (32767 * 1 / 100)) {
      exceptioncounter++;
      if (exceptioncounter >= (size * 2 / 100)) {
	zero_count = 0;
	break;
      }
    }
  }
  if (i == nsamples) {
    zero_count += size;
    if (zero_count / uade_sample_bytes_per_second >= uade_silence_timeout)
      return 1;
  }
  return 0;
}


static void filter_command(void)
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


static void interpolation_command(void)
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


int play_loop(void)
{
  uint16_t *sm;
  int i;
  uint32_t *u32ptr;

  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  int left;
  int subsong_end = 0;
  int next_song = 0;
  int ret;
  int cur_sub = -1, min_sub = -1, max_sub = -1, new_sub;
  int tailbytes = 0;
  int playbytes;
  char *reason;
  int64_t total_bytes = 0;
  int64_t subsong_bytes = 0;
  int64_t skip_bytes;
  int64_t time_bytes = 0;
  int deciseconds;
  int jump_sub = 0;

  int have_subsong_info = 0;

  int old_use_filter = uade_use_filter;
  int old_force_filter = uade_force_filter;
  int old_filter_state = uade_filter_state;

  filter_command();
  interpolation_command();

  /* skip bytes must be a multiple of audio frame size, which is 4 from the
     simulator */
  skip_bytes = uade_jump_pos * 4 * 44100;
  skip_bytes = (skip_bytes / 4) * 4;

  test_song_end_trigger(); /* clear a pending SIGINT */

  left = 0;
  enum uade_control_state state = UADE_S_STATE;

  while (next_song == 0) {

    if (uade_terminated) {
      if (!uade_no_output)
	tprintf("\n");
      return 0;
    }

    if (state == UADE_S_STATE) {

      if (skip_bytes == 0) {
	deciseconds = time_bytes * 10 / (44100 * 4);
	if (!uade_no_output) {
	  tprintf("Playing time position %d.%ds in subsong %d                \r", deciseconds / 10, deciseconds % 10,  cur_sub == -1 ? 0 : cur_sub);
	  fflush(stdout);
	}
      }

      if (uade_terminal_mode) {
	switch ((ret = poll_terminal())) {
	case 0:
	  break;
	case '.':
	  if (skip_bytes == 0) {
	    fprintf(stderr, "\nSkipping 10 seconds\n");
	    skip_bytes = 4 * 44100 * 10;
	  }
	  break;
	case ' ':
	case 'b':
	  subsong_end = 1;
	  break;
	case 'c':
	  pause_terminal();
	  break;
	case 'f':
	  uade_use_filter = 1;
	  uade_force_filter = 1;
	  uade_filter_state ^= 1;
	  tprintf("\nFilter %s\n", (uade_filter_state & 1) ? "on" : "off");
	  filter_command();
	  break;
	case 'h':
	  tprintf("\n\n");
	  print_action_keys();
	  tprintf("\n");
	  break;
	case '\n':
	case 'n':
	  uade_song_end_trigger = 1;
	  break;
	case 'q':
	  tprintf("\n");
	  return 0;
	case 's':
	  playlist_random(&uade_playlist, -1);
	  tprintf("\n%s mode\n", uade_playlist.randomize ? "Shuffle" : "Normal");
	  break;
	case 'x':
	  cur_sub--;
	  subsong_end = 1;
	  jump_sub = 1;
	  break;
	case 'z':
	  new_sub = cur_sub - 1;
	  if (new_sub < 0)
	    new_sub = 0;
	  if (min_sub >= 0 && new_sub < min_sub)
	    new_sub = min_sub;
	  cur_sub = new_sub - 1;
	  subsong_end = 1;
	  jump_sub = 1;
	  break;
	default:
	  if (isdigit(ret)) {
	    new_sub = ret - '0';
	    if (min_sub >= 0 && new_sub < min_sub) {
	      fprintf(stderr, "\ntoo low a subsong number\n");
	      break;
	    }
	    if (max_sub >= 0 && new_sub > max_sub) {
	      fprintf(stderr, "\ntoo high a subsong number\n");
	      break;
	    }
	    cur_sub = new_sub - 1;
	    subsong_end = 1;
	    jump_sub = 1;
	  } else if (!isspace(ret)) {
	    fprintf(stderr, "\n%c is not a valid command\n", ret);
	  }
	}
      }

      if (uade_debug_trigger == 1) {
	if (uade_send_short_message(UADE_COMMAND_ACTIVATE_DEBUGGER)) {
	  fprintf(stderr, "\ncan not active debugger\n");
	  return 0;
	}
	uade_debug_trigger = 0;
      }
      
      if (uade_info_mode && have_subsong_info) {
	/* we assume that subsong info is the last info we get */
	uade_song_end_trigger = 1;
	subsong_end = 0;
      }

      if (subsong_end && uade_song_end_trigger == 0) {
	if (jump_sub || (uade_one_subsong_per_file == 0 && cur_sub != -1 && max_sub != -1)) {
	  cur_sub++;
	  jump_sub = 0;
	  if (cur_sub > max_sub) {
	    uade_song_end_trigger = 1;
	  } else {
	    subsong_end = 0;
	    subsong_bytes = 0;
	    time_bytes = 0;
	    *um = (struct uade_msg) {.msgtype = UADE_COMMAND_CHANGE_SUBSONG,
				     .size = 4};
	    * (uint32_t *) um->data = htonl(cur_sub);
	    if (uade_send_message(um)) {
	      fprintf(stderr, "\ncould not change subsong\n");
	      exit(-1);
	    }
	    fprintf(stderr, "\nSubsong %d from range [%d, %d]\n", cur_sub, min_sub, max_sub);
	  }
	} else {
	  uade_song_end_trigger = 1;
	}
      }

      /* Check if control-c was pressed */
      if (uade_song_end_trigger) {
	next_song = 1;
	if (uade_send_short_message(UADE_COMMAND_REBOOT)) {
	  fprintf(stderr, "\ncan not send reboot\n");
	  return 0;
	}
	goto sendtoken;
      }

      left = UADE_MAX_MESSAGE_SIZE - sizeof(*um);
      um->msgtype = UADE_COMMAND_READ;
      um->size = 4;
      * (uint32_t *) um->data = htonl(left);
      if (uade_send_message(um)) {
	fprintf(stderr, "\ncan not send read command\n");
	return 0;
      }

    sendtoken:
      if (uade_send_short_message(UADE_COMMAND_TOKEN)) {
	fprintf(stderr, "\ncan not send token\n");
	return 0;
      }
      state = UADE_R_STATE;

    } else {

      /* receive state */

      if (uade_receive_message(um, sizeof(space)) <= 0) {
	fprintf(stderr, "\ncan not receive events from uade\n");
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

	if (subsong_end) {
	  playbytes = tailbytes;
	  tailbytes = 0;
	} else {
	  playbytes = um->size;
	}

	time_bytes += playbytes;

	if (uade_timeout != -1)
	  total_bytes += playbytes;

	if (uade_subsong_timeout != -1)
	  subsong_bytes += playbytes;

	if (skip_bytes > 0) {
	  if (playbytes <= skip_bytes) {
	    skip_bytes -= playbytes;
	    playbytes = 0;
	  } else {
	    playbytes -= skip_bytes;
	    memmove(um->data, ((uint8_t *) um->data) + skip_bytes, playbytes);
	    skip_bytes = 0;
	  }
	}

	if (uade_use_panning)
	  uade_effect_pan(um->data, playbytes, uade_bytes_per_sample, uade_panning_value);

	if (!audio_play(um->data, playbytes)) {
	  fprintf(stderr, "\nlibao error detected.\n");
	  return 0;
	}

	if (uade_timeout != -1) {
	  if (uade_song_end_trigger == 0) {
	    if (total_bytes / uade_sample_bytes_per_second >= uade_timeout) {
	      fprintf(stderr, "\nSong end (timeout %ds)\n", uade_timeout);
	      uade_song_end_trigger = 1;
	    }
	  }
	}

	if (uade_subsong_timeout != -1) {
	  if (subsong_end == 0 && uade_song_end_trigger == 0) {
	    if (subsong_bytes / uade_sample_bytes_per_second >= uade_subsong_timeout) {
	      fprintf(stderr, "\nSong end (subsong timeout %ds)\n", uade_subsong_timeout);
	      subsong_end = 1;
	    }
	  }
	}

	if (uade_test_silence(um->data, playbytes)) {
	  if (subsong_end == 0 && uade_song_end_trigger == 0) {
	    fprintf(stderr, "\nsilence detected (%d seconds)\n", uade_silence_timeout);
	    subsong_end = 1;
	  }
	}

	assert (left >= um->size);
	left -= um->size;
	break;
	
      case UADE_REPLY_FORMATNAME:
	uade_check_fix_string(um, 128);
	debug("\nFormat name: %s\n", (uint8_t *) um->data);
	if (uade_info_mode)
	  tprintf("formatname: %s\n", (char *) um->data);
	break;
	
      case UADE_REPLY_MODULENAME:
	uade_check_fix_string(um, 128);
	debug("\nModule name: %s\n", (uint8_t *) um->data);
	if (uade_info_mode)
	  tprintf("modulename: %s\n", (char *) um->data);
	break;

      case UADE_REPLY_MSG:
	uade_check_fix_string(um, 128);
	debug("\nMessage: %s\n", (char *) um->data);
	break;

      case UADE_REPLY_PLAYERNAME:
	uade_check_fix_string(um, 128);
	debug("\nPlayer name: %s\n", (uint8_t *) um->data);
	if (uade_info_mode)
	  tprintf("playername: %s\n", (char *) um->data);
	break;

      case UADE_REPLY_SONG_END:
	if (um->size < 9) {
	  fprintf(stderr, "\nillegal song end reply\n");
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
	  uade_song_end_trigger = 1;
	}
	i = 0;
	reason = &((uint8_t *) um->data)[8];
	while (reason[i] && i < (um->size - 8))
	  i++;
	if (reason[i] != 0 || (i != (um->size - 9))) {
	  fprintf(stderr, "\nbroken reason string with song end notice\n");
	  exit(-1);
	}
	fprintf(stderr, "\nSong end (%s)\n", reason);
	break;

      case UADE_REPLY_SUBSONG_INFO:
	if (um->size != 12) {
	  fprintf(stderr, "\nsubsong info: too short a message\n");
	  exit(-1);
	}
	u32ptr = (uint32_t *) um->data;
	debug("\nsubsong: %d from range [%d, %d]\n", u32ptr[2], u32ptr[0], u32ptr[1]);
	min_sub = u32ptr[0];
	max_sub = u32ptr[1];
	cur_sub = u32ptr[2];
	assert(-1 <= min_sub && min_sub <= cur_sub && cur_sub <= max_sub);
	if ((max_sub - min_sub) != 0)
	  fprintf(stderr, "\nThere are %d subsongs in range [%d, %d].\n", 1 + max_sub - min_sub, min_sub, max_sub);
	have_subsong_info = 1;
	if (uade_info_mode)
	  tprintf("subsong_info: %d %d %d (cur, min, max)\n", cur_sub, min_sub, max_sub);
	break;
	
      default:
	fprintf(stderr, "\nuade123: expected sound data. got %d.\n", um->msgtype);
	return 0;
      }
    }
  }

  do {
    ret = uade_receive_message(um, sizeof(space));
    if (ret < 0) {
      fprintf(stderr, "\nuade123: can not receive events (TOKEN) from uade\n");
      return 0;
    }
    if (ret == 0) {
      fprintf(stderr, "\nuade123: end of input after reboot\n");
      return 0;
    }
  } while (um->msgtype != UADE_COMMAND_TOKEN);

  uade_use_filter = old_use_filter;
  uade_force_filter = old_force_filter;
  uade_filter_state = old_filter_state;

  tprintf("\n");
  return 1;
}
