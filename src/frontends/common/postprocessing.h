#ifndef _UADE_POSTPROCESSING_H_
#define _UADE_POSTPROCESSING_H_

enum uade_postprocessing_op {
  UADE_POSTPROCESSING_ENABLE = 1,
  UADE_POSTPROCESSING_DISABLE,
  UADE_POSTPROCESSING_TOGGLE,
  UADE_GAIN_ENABLE,
  UADE_GAIN_DISABLE,
  UADE_GAIN_TOGGLE,
  UADE_HEADPHONES_ENABLE,
  UADE_HEADPHONES_DISABLE,
  UADE_HEADPHONES_TOGGLE,
  UADE_PANNING_ENABLE,
  UADE_PANNING_DISABLE,
  UADE_PANNING_TOGGLE
};

extern float uade_gain_value;
extern float uade_panning_value;
extern int uade_use_gain;
extern int uade_use_headphones;
extern int uade_use_panning;
extern int uade_use_postprocessing;

void uade_postprocessing_setup(enum uade_postprocessing_op op);

#endif
