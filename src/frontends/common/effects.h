#ifndef _UADE2_EFFECTS_H_
#define _UADE2_EFFECTS_H_

#include <stdint.h>

typedef enum {
    UADE_EFFECT_PAN,
    UADE_EFFECT_HEADPHONES
} uade_effect_t;

void uade_effect_disable(uade_effect_t effect);
void uade_effect_disable_all(void);
void uade_effect_enable(uade_effect_t effect);
/* effect-specific knobs */
void uade_effect_pan_set_amount(float amount);
/* reset state at start of song */
void uade_effect_reset_internals(void);
/* process n frames of sample buffer */
void uade_effect_run(int16_t *sample, int frames);

#define UADE_EFFECT_HEADPHONES_DELAY_LENGTH 22
#define UADE_EFFECT_HEADPHONES_DELAY_DIRECT 0.3
#define UADE_EFFECT_HEADPHONES_CROSSMIX_VOL 0.80

#endif
