#ifndef _UADECONTROL_H_
#define _UADECONTROL_H_

#include <stdlib.h>

#include <uadeipc.h>

extern int uade_input_fd; /* stdin */
extern int uade_output_fd; /* stdout */

int uade_get_command(struct uade_control *uc, size_t maxbytes);
void uade_set_input_source(const char *input_source);
void uade_set_output_destination(const char *output_destination);
int uade_get_string_command(char *s, enum uade_command_t com, size_t maxlen);

#endif
