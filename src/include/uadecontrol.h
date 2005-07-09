#ifndef _UADECONTROL_H_
#define _UADECONTROL_H_

#include <stdlib.h>

#include <uadeipc.h>

extern int uade_input_fd; /* stdin */
extern int uade_output_fd; /* stdout */

int uade_receive_message(struct uade_msg *uc, size_t maxbytes);
int uade_receive_string(char *s, enum uade_msgtype msgtype, size_t maxlen);
int uade_send_message(struct uade_msg *uc);
int uade_send_string(enum uade_msgtype msgtype, const char *str);
void uade_set_input_source(const char *input_source);
void uade_set_output_destination(const char *output_destination);

#endif
