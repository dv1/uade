#ifndef _UADEIPC_H_
#define _UADEIPC_H_

#include <stdlib.h>

#include <uademsg.h>

enum uade_control_state {
  UADE_INITIAL_STATE = 0,
  UADE_R_STATE,
  UADE_S_STATE
};

struct uade_ipc {
  void *input;
  void *output;
  unsigned int inputbytes;
  char inputbuffer[UADE_MAX_MESSAGE_SIZE];
  enum uade_control_state state;
};

void uade_check_fix_string(struct uade_msg *um, size_t maxlen);
int uade_receive_message(struct uade_msg *um, size_t maxbytes, struct uade_ipc *ipc);
int uade_receive_short_message(enum uade_msgtype msgtype, struct uade_ipc *ipc);
int uade_receive_string(char *s, enum uade_msgtype msgtype, size_t maxlen, struct uade_ipc *ipc);
int uade_send_message(struct uade_msg *um, struct uade_ipc *ipc);
int uade_send_short_message(enum uade_msgtype msgtype, struct uade_ipc *ipc);
int uade_send_string(enum uade_msgtype msgtype, const char *str, struct uade_ipc *ipc);
void uade_set_peer(struct uade_ipc *ipc, int peer_is_client, const char *input, const char *output);

#endif
