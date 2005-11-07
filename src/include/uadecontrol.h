#ifndef _UADECONTROL_H_
#define _UADECONTROL_H_

#include <stdlib.h>

#include <uademsg.h>


enum uade_control_state {
  UADE_INITIAL_STATE = 0,
  UADE_R_STATE,
  UADE_S_STATE
};


extern int uade_input_fd; /* stdin */
extern int uade_output_fd; /* stdout */

void uade_check_fix_string(struct uade_msg *um, size_t maxlen);
int uade_receive_message(struct uade_msg *um, size_t maxbytes);
int uade_receive_short_message(enum uade_msgtype msgtype);
int uade_receive_string(char *s, enum uade_msgtype msgtype, size_t maxlen);
int uade_send_message(struct uade_msg *um);
int uade_send_short_message(enum uade_msgtype msgtype);
int uade_send_string(enum uade_msgtype msgtype, const char *str);
void uade_set_input_source(const char *input_source);
void uade_set_output_destination(const char *output_destination);

#endif
