#ifndef _UADE_STATE_H_
#define _UADE_STATE_H_

#include <eagleplayer.h>
#include <effects.h>

struct uade_state {
	/* Per song members */
	struct uade_config config;
	struct uade_song *song;
	struct uade_effect *effect;
	struct eagleplayer *ep;

	/* Permanent members */
	int validconfig;
	struct eagleplayerstore *playerstore;
};

#endif
