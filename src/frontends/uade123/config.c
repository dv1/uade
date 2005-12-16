/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "config.h"
#include "uade123.h"
#include "postprocessing.h"


void post_config(struct uade_config *uc)
{
  uade_post_config(uc);

  if (uc->action_keys)
    uade_terminal_mode = uc->action_keys & 1;

  if (uc->filter)
    uade_use_filter = uc->filter;

  if (uc->force_led) {
    uade_force_filter = 1;
    uade_filter_state = uc->force_led & 1;
  }

  if (uc->gain != 1.0) {
    uade_gain_value = uc->gain;
    uade_postprocessing_setup(UADE_GAIN_ENABLE);
  }

  if (uc->headphones)
    uade_postprocessing_setup(UADE_HEADPHONES_ENABLE);

  if (uc->ignore_player_check)
    uade_ignore_player_check = 1;

  if (uc->interpolator)
    uade_interpolation_mode = strdup(uc->interpolator);

  if (uc->no_filter)
    uade_use_filter = 0;

  if (uc->one_subsong)
    uade_one_subsong_per_file = 1;

  if (uc->panning != 0.0) {
    uade_panning_value = uc->panning;
    uade_postprocessing_setup(UADE_PANNING_ENABLE);
  }

  if (uc->random_play)
    playlist_random(&uade_playlist, 1);

  if (uc->recursive_mode)
    uade_recursivemode = 1;

  if (uc->silence_timeout)
    uade_silence_timeout = uc->silence_timeout;

  if (uc->subsong_timeout)
    uade_subsong_timeout = uc->subsong_timeout;

  if (uc->timeout)
    uade_timeout = uc->timeout;
}
