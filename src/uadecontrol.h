#ifndef _UADECONTROL_H_
#define _UADECONTROL_H_

#include <stdlib.h>

#include <uadeipc.h>

int uade_get_command(struct uade_control *uc, size_t maxbytes);

#endif
