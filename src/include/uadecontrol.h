#ifndef _UADECONTROL_H_
#define _UADECONTROL_H_

#include <stdlib.h>

#include <uadeipc.h>

extern int uade_input_fd; /* stdin */
extern int uade_output_fd; /* stdout */

int uade_receive_command(struct uade_control *uc, size_t maxbytes);
int uade_send_command(struct uade_control *uc);
int uade_send_string(enum uade_command com, const char *str);
void uade_set_input_source(const char *input_source);
void uade_set_output_destination(const char *output_destination);
int uade_receive_string_command(char *s, enum uade_command com, size_t maxlen);

#endif
