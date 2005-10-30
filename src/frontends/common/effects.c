/* Effect module for UADE2 frontends.

   Copyright 2005 (C) Antti S. Lankila <alankila@bel.fi>

   This module is licensed under the GNU LGPL.
*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "effects.h"


static int uade_effect_enabled = 0;

static int uade_effect_pan_amount = 0;

static float uade_effect_headphones_ap_l[UADE_EFFECT_HEADPHONES_DELAY_LENGTH];
static float uade_effect_headphones_ap_r[UADE_EFFECT_HEADPHONES_DELAY_LENGTH];
static float uade_effect_headphones_bq_l[4];
static float uade_effect_headphones_bq_r[4];

static void uade_effect_pan(int16_t *sm, int frames);
static void uade_effect_headphones(int16_t *sm, int frames);


/* Reset effects' state variables.
 * Call this method between before starting playback */
void uade_effect_reset_internals(void)
{
    memset(uade_effect_headphones_ap_l, 0, sizeof(uade_effect_headphones_ap_l));
    memset(uade_effect_headphones_ap_r, 0, sizeof(uade_effect_headphones_ap_r));
    memset(uade_effect_headphones_bq_l, 0, sizeof(uade_effect_headphones_bq_l));
    memset(uade_effect_headphones_bq_r, 0, sizeof(uade_effect_headphones_bq_r));
}

void uade_effect_disable_all(void)
{
    uade_effect_enabled = 0;
}

void uade_effect_enable(uade_effect_t effect)
{
    uade_effect_enabled |= (1 << effect);
}

void uade_effect_disable(uade_effect_t effect)
{
    uade_effect_enabled &= ~(1 << effect);
}

void uade_effect_run(int16_t *samples, int frames)
{
    if (uade_effect_enabled & (1 << UADE_EFFECT_PAN))
        uade_effect_pan(samples, frames);
    if (uade_effect_enabled & (1 << UADE_EFFECT_HEADPHONES))
        uade_effect_headphones(samples, frames);
}

void uade_effect_pan_set_amount(float amount)
{
    assert(amount >= 0.0 && amount <= 2.0);
    uade_effect_pan_amount = amount * 256.0 / 2.0;
}


/* Panning effect. Turns stereo into mono in a specific degree */
static void uade_effect_pan(int16_t *sm, int frames)
{
  int i, l, r, m;
  for (i = 0; i < frames; i += 1) {
    l = sm[0];
    r = sm[1];
    m = (r - l) * uade_effect_pan_amount;
    sm[0] = ( ((l << 8) + m) >> 8 );
    sm[1] = ( ((r << 8) - m) >> 8 );
    sm += 2;
  }
}

/* All-pass delay. Its purpose is to confuse the phase of the sound a bit
 * and also provide some delay to locate the source outside the head. This
 * seems to work better than a pure delay line. */
static float uade_effect_headphones_allpass_delay(float in, float *state)
{
    int i;
    float tmp, output;

    tmp = in - UADE_EFFECT_HEADPHONES_DELAY_DIRECT * state[0];
    output = state[0] + UADE_EFFECT_HEADPHONES_DELAY_DIRECT * tmp;

    /* FIXME: use modulo and index */
    for (i = 1; i < UADE_EFFECT_HEADPHONES_DELAY_LENGTH; i += 1)
        state[i - 1] = state[i];
    state[UADE_EFFECT_HEADPHONES_DELAY_LENGTH - 1] = tmp;

    return output;
}

/* -6 dB/oct curve to approximate the audio filtering for the other ear */
static float uade_effect_headphones_lpf(float in, float *state)
{
    float out = in * 0.43;
    out += 0.57 * state[0];
    state[0] = out;

    return out;
}

/* A real implementation would simply perform FIR with recorded HRTF data. */
static void uade_effect_headphones(int16_t *sm, int frames)
{
    int i;
    for (i = 0; i < frames; i += 1) {
	float l = uade_effect_headphones_allpass_delay(sm[0], uade_effect_headphones_ap_l);
	float r = uade_effect_headphones_allpass_delay(sm[1], uade_effect_headphones_ap_r);

	l = uade_effect_headphones_lpf(l, uade_effect_headphones_bq_l);
	r = uade_effect_headphones_lpf(r, uade_effect_headphones_bq_r);

	sm[0] = (sm[0] + r * UADE_EFFECT_HEADPHONES_CROSSMIX_VOL) / (1 + UADE_EFFECT_HEADPHONES_CROSSMIX_VOL);
	sm[1] = (sm[1] + l * UADE_EFFECT_HEADPHONES_CROSSMIX_VOL) / (1 + UADE_EFFECT_HEADPHONES_CROSSMIX_VOL);
	sm += 2;
    }
}

