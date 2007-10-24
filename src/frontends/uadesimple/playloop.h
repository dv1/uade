#ifndef _UADE123_PLAYLOOP_H_
#define _UADE123_PLAYLOOP_H_

#include <uadeconf.h>
#include <uadeipc.h>
#include <eagleplayer.h>

int play_loop(struct uade_ipc *ipc, struct uade_song *us,
	      struct uade_effect *ue, struct uade_config *uc);

#endif
