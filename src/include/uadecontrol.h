/* UADE
 *
 * Copyright 2005 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) however you
 * want in your projects.
 */

#ifndef _UADECONTROL_H_
#define _UADECONTROL_H_

#include <stdlib.h>

#include <uademsg.h>

extern int uade_input_fd; /* stdin */
extern int uade_output_fd; /* stdout */

void uade_check_fix_string(struct uade_msg *um, size_t maxlen);
int uade_receive_message(struct uade_msg *um, size_t maxbytes);
int uade_receive_string(char *s, enum uade_msgtype msgtype, size_t maxlen);
int uade_send_message(struct uade_msg *um);
int uade_send_string(enum uade_msgtype msgtype, const char *str);
void uade_set_input_source(const char *input_source);
void uade_set_output_destination(const char *output_destination);

#endif
