#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <uadecontrol.h>
#include <uadeipc.h>
#include <unixatomic.h>


void uade_spawn(pid_t *uadepid, const char *uadename, int debug_mode)
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

