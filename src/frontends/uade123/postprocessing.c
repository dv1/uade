
#include <stdlib.h>
#include <stdio.h>

#include "effects.h"
#include "postprocessing.h"

float uade_panning_value = 0.7;
int uade_use_headphones;
int uade_use_panning;
int uade_use_postprocessing;


void uade_postprocessing_setup(enum uade_postprocessing_op op)
{
  uade_effect_disable_all();

  switch (op) {
  case UADE_POSTPROCESSING_ENABLE:
    if (uade_use_postprocessing == 0)
      uade_effect_reset_internals();
    uade_use_postprocessing = 1;
    break;
  case UADE_POSTPROCESSING_TOGGLE:
    if (uade_use_postprocessing == 0)
      uade_effect_reset_internals();
    uade_use_postprocessing ^= 1;
    break;
  case UADE_PANNING_ENABLE:
    uade_use_panning = 1;
    break;
  case UADE_PANNING_TOGGLE:
    uade_use_panning ^= 1;
    break;
  case UADE_HEADPHONES_ENABLE:
    uade_use_headphones = 1;
    break;
  case UADE_HEADPHONES_TOGGLE:
    uade_use_headphones ^= 1;
    break;
  default:
    fprintf(stderr, "Illegal postprocessing set parameter.\n");
    exit(-1);
  }

  if (uade_use_postprocessing) {
    if (uade_use_headphones)
      uade_effect_enable(UADE_EFFECT_HEADPHONES);

    if (uade_use_panning) {
      uade_effect_pan_set_amount(uade_panning_value);
      uade_effect_enable(UADE_EFFECT_PAN);
    }
  }
}
