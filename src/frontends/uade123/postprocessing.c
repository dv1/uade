
#include <stdlib.h>
#include <stdio.h>

#include "effects.h"
#include "postprocessing.h"

int uade_use_headphones;
int uade_use_panning;
float uade_panning_value;
int uade_use_postprocessing;

/* set == 1  => enable
   set == 0  => disable
   set == -1 => toggle
*/
void uade_postprocessing_setup(int set)
{
  uade_effect_disable_all();

  if (set == 1) {
    if (uade_use_postprocessing == 0)
      uade_effect_reset_internals();
    uade_use_postprocessing = 1;
  } else if (set == 0) {
    uade_use_postprocessing = 0;
  } else if (set == -1) {
    uade_use_postprocessing ^= 1;
  } else {
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
