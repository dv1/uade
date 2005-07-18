#include <stdlib.h>
#include <stdio.h>

#include "effects.h"

/* this is b0rken. have support for 8 bit samples also. */
void uade_effect_pan(void *samples, int bytes, int bytes_per_sample,
		     float val)
{
  int16_t *sm = samples;
  int i, l, r, m;
  int mixpar = val * (256.0 / 2.0);
  int nsamples = bytes / bytes_per_sample;
  if (bytes_per_sample == 2) {
    for (i = 0; i < nsamples; i++) {
      l = (int) sm[0];
      r = (int) sm[1];
      m = (r - l) * mixpar;
      sm[0] = (short) ( ((l << 8) + m) >> 8 );
      sm[1] = (short) ( ((r << 8) - m) >> 8 );
      sm += 2;
    }
  } else {
    fprintf(stderr, "panning not supported with %d bit samples\n", bytes_per_sample * 8);
    exit(-1);
  }
}
