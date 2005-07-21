/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>

#include "uade123.h"
#include "effects.h"


int play_loop(void)
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
  int cur_sub = -1, min_sub = -1, max_sub = -1;
  int tailbytes = 0;
  int playbytes;
  char *reason;
  int64_t total_bytes = 0;
  int64_t subsong_bytes = 0;

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
	  if (one_subsong_per_file == 0 && cur_sub != -1 && max_sub != -1) {
	    cur_sub++;
	    if (cur_sub > max_sub) {
	      song_end_trigger = 1;
	    } else {
	      song_end = 0;
	      subsong_bytes = 0;
	      *um = (struct uade_msg) {.msgtype = UADE_COMMAND_CHANGE_SUBSONG,
				       .size = 4};
	      * (uint32_t *) um->data = htonl(cur_sub);
	      if (uade_send_message(um)) {
		fprintf(stderr, "could not change subsong\n");
		exit(-1);
	      }
	      fprintf(stderr, "subsong: %d from range [%d, %d]\n", cur_sub, min_sub, max_sub);
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
	  playbytes = tailbytes;
	  tailbytes = 0;
	} else {
	  playbytes = um->size;
	}

	if (use_panning)
	  uade_effect_pan(um->data, playbytes, bytes_per_sample, panning_value);

	if (!ao_play(libao_device, um->data, playbytes)) {
	  fprintf(stderr, "libao error detected.\n");
	  return 0;
	}
	if (timeout_value != -1) {
	  total_bytes += playbytes;
	  if (song_end_trigger == 0) {
	    if (total_bytes / sample_bytes_per_second >= timeout_value) {
	      fprintf(stderr, "song end (timeout %ds)\n", timeout_value);
	      song_end_trigger = 1;
	    }
	  }
	}
	if (subsong_timeout_value != -1) {
	  subsong_bytes += playbytes;
	  if (song_end == 0 && song_end_trigger == 0) {
	    if (subsong_bytes / sample_bytes_per_second >= subsong_timeout_value) {
	      fprintf(stderr, "song end (subsong timeout %ds)\n", subsong_timeout_value);
	      song_end = 1;
	    }
	  }
	}
	left -= um->size;
	break;
	
      case UADE_REPLY_FORMATNAME:
	uade_check_fix_string(um, 128);
	debug("format name: %s\n", (uint8_t *) um->data);
	break;
	
      case UADE_REPLY_MODULENAME:
	uade_check_fix_string(um, 128);
	debug("module name: %s\n", (uint8_t *) um->data);
	break;

      case UADE_REPLY_MSG:
	uade_check_fix_string(um, 128);
	debug("message: %s\n", (char *) um->data);
	break;
	
      case UADE_REPLY_PLAYERNAME:
	uade_check_fix_string(um, 128);
	debug("player name: %s\n", (uint8_t *) um->data);
	break;

      case UADE_REPLY_SONG_END:
	if (um->size < 9) {
	  fprintf(stderr, "illegal song end reply\n");
	  exit(-1);
	}
	tailbytes = ntohl(((uint32_t *) um->data)[0]);
	/* next ntohl() is only there for a principle. it is not useful */
	if (ntohl(((uint32_t *) um->data)[1]) == 0) {
	  /* normal happy song end. go to next subsong if any */
	  song_end = 1;
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
	  fprintf(stderr, "broken reason string with song end notice\n");
	  exit(-1);
	}
	fprintf(stderr, "song end (%s)\n", reason);
	break;

      case UADE_REPLY_SUBSONG_INFO:
	if (um->size != 12) {
	  fprintf(stderr, "subsong info: too short a message\n");
	  exit(-1);
	}
	u32ptr = (uint32_t *) um->data;
	debug("subsong: %d from range [%d, %d]\n", u32ptr[2], u32ptr[0], u32ptr[1]);
	min_sub = u32ptr[0];
	max_sub = u32ptr[1];
	cur_sub = u32ptr[2];
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
