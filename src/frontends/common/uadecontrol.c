/* uadecontrol.c is a helper module to control uade core through IPC:

   Copyright (C) 2005 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/


#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <uadecontrol.h>
#include <uadeipc.h>
#include <unixatomic.h>


void uade_send_filter_command(int filter_type, int filter_state, int force_filter)
{
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  *um = (struct uade_msg) {.msgtype = UADE_COMMAND_FILTER, .size = 8};
  ((uint32_t *) um->data)[0] = htonl(filter_type);
  if (force_filter == 0) {
    ((uint32_t *) um->data)[1] = htonl(0);
  } else {
    ((uint32_t *) um->data)[1] = htonl(2 + (filter_state & 1));
  }
  if (uade_send_message(um)) {
    fprintf(stderr, "Can not setup filters.\n");
    exit(-1);
  }
}


void uade_send_interpolation_command(const char *mode)
{
  if (mode != NULL) {
    if (strlen(mode) == 0) {
      fprintf(stderr, "Interpolation mode may not be empty.\n");
      exit(-1);
    }
    if (uade_send_string(UADE_COMMAND_SET_INTERPOLATION_MODE, mode)) {
      fprintf(stderr, "Can not set interpolation mode.\n");
      exit(-1);
    }
  }
}


static void subsong_control(int subsong, int command)
{
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  assert(subsong >= 0 && subsong < 256);

  *um = (struct uade_msg) {.msgtype = command, .size = 4};
  * (uint32_t *) um->data = htonl(subsong);
  if (uade_send_message(um) < 0) {
    fprintf(stderr, "Could not changet subsong\n");
    exit(-1);
  }
}


void uade_change_subsong(int subsong)
{
  subsong_control(subsong, UADE_COMMAND_CHANGE_SUBSONG);
}


void uade_set_subsong(int subsong)
{
  subsong_control(subsong, UADE_COMMAND_SET_SUBSONG);
}


int uade_song_initialization(const char *scorename,
			     const char *playername,
			     const char *modulename)
{
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *um = (struct uade_msg *) space;

  if (uade_send_string(UADE_COMMAND_SCORE, scorename)) {
    fprintf(stderr, "Can not send score name.\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_PLAYER, playername)) {
    fprintf(stderr, "Can not send player name.\n");
    goto cleanup;
  }

  if (uade_send_string(UADE_COMMAND_MODULE, modulename)) {
    fprintf(stderr, "Can not send module name.\n");
    goto cleanup;
  }

  if (uade_send_short_message(UADE_COMMAND_TOKEN)) {
    fprintf(stderr, "Can not send token after module.\n");
    goto cleanup;
  }

  if (uade_receive_message(um, sizeof(space)) <= 0) {
    fprintf(stderr, "Can not receive acknowledgement.\n");
    goto cleanup;
  }

  if (um->msgtype == UADE_REPLY_CANT_PLAY) {
    if (uade_receive_short_message(UADE_COMMAND_TOKEN)) {
      fprintf(stderr, "Can not receive token in main loop.\n");
      exit(-1);
    }
    return UADECORE_CANT_PLAY;
  }

  if (um->msgtype != UADE_REPLY_CAN_PLAY) {
    fprintf(stderr, "Unexpected reply from uade: %d\n", um->msgtype);
    goto cleanup;
  }

  if (uade_receive_short_message(UADE_COMMAND_TOKEN) < 0) {
    fprintf(stderr, "Can not receive token after play ack.\n");
    goto cleanup;
  }

  return 0;

 cleanup:
    return UADECORE_INIT_ERROR;
}


void uade_spawn(pid_t *uadepid, const char *uadename, const char *configname,
		int debug_mode)
{
  int forwardfds[2];
  int backwardfds[2];
  char url[64];

  if (pipe(forwardfds) != 0 || pipe(backwardfds) != 0) {
    fprintf(stderr, "Can not create pipes: %s\n", strerror(errno));
    exit(-1);
  }
 
  *uadepid = fork();
  if (*uadepid < 0) {
    fprintf(stderr, "Fork failed: %s\n", strerror(errno));
    exit(-1);
  }
  if (*uadepid == 0) {
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
    fprintf(stderr, "Execlp failed: %s\n", strerror(errno));
    abort();
  }

  /* close fd that uade reads from and writes to */
  if (atomic_close(forwardfds[0]) < 0 || atomic_close(backwardfds[1]) < 0) {
    fprintf(stderr, "Could not close uade fds: %s\n", strerror(errno));
    kill (*uadepid, SIGTERM);
    *uadepid = 0;
    exit(-1);
  }

  /* write destination */
  snprintf(url, sizeof(url), "fd://%d", forwardfds[1]);
  uade_set_output_destination(url);
  /* read source */
  snprintf(url, sizeof(url), "fd://%d", backwardfds[0]);
  uade_set_input_source(url);

  if (uade_send_string(UADE_COMMAND_CONFIG, configname)) {
    fprintf(stderr, "Can not send config name.\n");
    kill(*uadepid, SIGTERM);
    *uadepid = 0;
    exit(-1);
  }
}

