 /*
  * UAE - The Un*x Amiga Emulator
  *
  * OS specific functions
  *
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  * Copyright 1996 Marcus Sundberg
  * Copyright 1996 Manfred Thole
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "gensound.h"
#include "sd-sound.h"
#include "events.h"
#include "cia.h"
#include "audio.h"
#include "amigafilter.h"
#include "uade.h"

struct audio_channel_data audio_channel[4];
int sound_available;
void (*sample_handler) (void);
unsigned long sample_evtime;

int sound_use_filter = FILTER_MODEL_A1200;

static unsigned long last_cycles, next_sample_evtime;
static int audperhack;

static float sound_left_input[4];
static float sound_left_output[4];
static float sound_right_input[4];
static float sound_right_output[4];

/* Amiga has two separate filtering circuits per channel, a static RC filter
 * on A500 and the LED filter. This code emulates both.
 * 
 * The Amiga filtering circuitry depends on Amiga model. Older Amigas seem
 * to have a 6 dB/oct RC filter with cutoff frequency such that the -6 dB
 * point for filter is reached at 6 kHz, while newer Amigas have no filtering.
 *
 * The LED filter is complicated, and we are modelling it with a pair of
 * RC filters, the other providing a highboost. The LED starts to cut
 * into signal somewhere around 5-6 kHz, and there's some kind of highboost
 * in effect above 12 kHz. Better measurements are required.
 *
 * The current filtering should be accurate to 2 dB with the filter on,
 * and to 1 dB with the filter off.
*/

static int filter(int data, float *input, float *output)
{
    int o;
    float s;

    if (sound_use_filter == FILTER_MODEL_A500) {
	s  = 0.36 * data;
	s += 0.64 * output[2];
	output[2] = s;
    } else if (sound_use_filter == FILTER_MODEL_A1200) {
	output[2] = data;
    } else {
	fprintf(stderr, "Unknown filter mode\n");
	exit(-1);
    }

    /* output[0] is output[2] through lowpass */
    s  = 0.33 * output[2];
    s += 0.67 * output[0];
    output[0] = s;

    /* output[1] is output[2] with slight highboost */
    s  = 1.35 * output[0];
    s -= 0.35 * output[1];
    output[1] = s;

    if (!gui_ledstate) {
	o = output[2];
    } else {
	o = output[1] * 0.98; /* to avoid overruns */
    }

    if (o > 32767) {
	o = 32767;
    } else if (o < -32768) {
	o = -32768;
    }

    return o;
}

static void check_sound_buffers (void)
{
    if (uade_reboot)
	return;
    assert(uade_read_size > 0);
    intptr_t bytes = ((intptr_t) sndbufpt) - ((intptr_t) sndbuffer);
    if (uade_audio_output) {
	if (bytes == 2048 || bytes == uade_read_size) {
	    uade_check_sound_buffers(uade_read_size > 2048 ? 2048 : uade_read_size);
	    sndbufpt = sndbuffer;
	}
    } else {
	uade_audio_skip += bytes;
	/* if sound core doesn't report audio output start in 3 seconds from
	   the reboot, begin audio output anyway */
	if (uade_audio_skip >= (sound_bytes_per_second * 3)) {
	    fprintf(stderr, "involuntary audio output start\n");
	    uade_audio_output = 1;
	}
	sndbufpt = sndbuffer;
    }
}

static inline void sample_backend(int left, int right)
{
    /* samples are in range -16384 (-128*64*2) and 16256 (127*64*2) */
    left <<= 16 - 14 - 1;
    right <<= 16 - 14 - 1;
    /* [-32768, 32512] */

    if (sound_use_filter) {
	left = filter(left, sound_left_input, sound_left_output);
	right = filter(right, sound_right_input, sound_right_output);
    }

    *(sndbufpt++) = left;
    *(sndbufpt++) = right;

    check_sound_buffers();
}


void sample16s_handler (void)
{
    int datas[4];
    int i;

    for (i = 0; i < 4; i++) {
	datas[i] = audio_channel[i].current_sample * audio_channel[i].vol;
	datas[i] &= audio_channel[i].adk_mask;
    }

    sample_backend(datas[0] + datas[3], datas[1] + datas[2]);
}

void sample16si_crux_handler (void)
{
    int i;
    int datas[4];

    for (i = 0; i < 4; i += 1) {
        int ratio1 = audio_channel[i].per - audio_channel[i].evtime;
#define INTERVAL (sample_evtime * 3)
	int ratio = (ratio1 << 12) / INTERVAL;
	if (audio_channel[i].evtime < sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
#undef INTERVAL
	datas[i] = ((       ratio) * audio_channel[i].current_sample
		    + (4096 - ratio) * audio_channel[i].last_sample[0]) >> 12;
	datas[i] *= audio_channel[i].vol;
        datas[i] &= audio_channel[i].adk_mask;
    }

    sample_backend(datas[0] + datas[3], datas[1] + datas[2]);
}

void sample16si_linear_handler (void)
{
    int i;
    int datas[4];

    for (i = 0; i < 4; i += 1) {
        int period = audio_channel[i].per;
        int position = ((audio_channel[i].evtime % period) << 8) / period;
        datas[i] = ((      position) * audio_channel[i].last_sample[0]
                  + (256 - position) * audio_channel[i].current_sample) >> 8;
	datas[i] *= audio_channel[i].vol;
        datas[i] &= audio_channel[i].adk_mask;
    }

    sample_backend(datas[0] + datas[3], datas[1] + datas[2]);
}

/* Interpolation matrix

       part       x**3    x**2    x**1    x**0
       Y[IP-1]    -0.5     1      -0.5    0
       Y[IP]       1.5    -2.5     0      1
       Y[IP+1]    -1.5     2       0.5    0
       Y[IP+2]     0.5    -0.5     0      0

  See libmodplug fastmix.cpp for derivation.
 */
int sample16si_cspline_interpolate_one(int *last, int current, float x)
{
    float x2 = x * x;
    float x3 = x * x * x;
    return  ((                 1.0 * last[0]                                )
        + x *(-0.5 * current                 + 0.5 * last[1]                )
        + x2*( 1.0 * current - 2.5 * last[0] + 2.0 * last[1] - 0.5 * last[2])
        + x3*(-0.5 * current + 1.5 * last[0] - 1.5 * last[1] + 0.5 * last[2]));
}

void sample16si_cspline_handler (void)
{
    int i;
    int datas[4];

    for (i = 0; i < 4; i += 1) {
        int period = audio_channel[i].per;
        datas[i] = sample16si_cspline_interpolate_one(
            audio_channel[i].last_sample,
            audio_channel[i].current_sample,
            (audio_channel[i].evtime % period) / (1.0 * period)
        );
	datas[i] *= audio_channel[i].vol;
        datas[i] &= audio_channel[i].adk_mask;
    }

    sample_backend(datas[0] + datas[3], datas[1] + datas[2]);
}


static void audio_handler (int nr)
{
    struct audio_channel_data *cdp = audio_channel + nr;

    switch (cdp->state) {
     case 0:
	fprintf(stderr, "Bug in sound code\n");
	break;

     case 1:
	/* We come here at the first hsync after DMA was turned on. */
	cdp->evtime = maxhpos;

	cdp->state = 5;
	INTREQ(0x8000 | (0x80 << nr));
	if (cdp->wlen != 1)
	    cdp->wlen--;
	cdp->nextdat = chipmem_bank.wget(cdp->pt);

	cdp->pt += 2;
	break;

     case 5:
	/* We come here at the second hsync after DMA was turned on. */
	if (currprefs.produce_sound == 0)
	    cdp->per = 65535;

	cdp->evtime = cdp->per;
	cdp->dat = cdp->nextdat;
        cdp->last_sample[2] = cdp->last_sample[1];
        cdp->last_sample[1] = cdp->last_sample[0];
	cdp->last_sample[0] = cdp->current_sample;
	cdp->current_sample = (uae_s8)(cdp->dat >> 8);

	cdp->state = 2;
	{
	    int audav = adkcon & (1 << nr);
	    int audap = adkcon & (16 << nr);
	    int napnav = (!audav && !audap) || audav;
	    if (napnav)
		cdp->data_written = 2;
	}
	break;

     case 2:
	/* We come here when a 2->3 transition occurs */
	if (currprefs.produce_sound == 0)
	    cdp->per = 65535;

	cdp->last_sample[2] = cdp->last_sample[1];
	cdp->last_sample[1] = cdp->last_sample[0];
	cdp->last_sample[0] = cdp->current_sample;
	cdp->current_sample = (uae_s8)(cdp->dat & 0xFF);
	cdp->evtime = cdp->per;

	cdp->state = 3;

	/* Period attachment? */
	if (adkcon & (0x10 << nr)) {
	    if (cdp->intreq2 && cdp->dmaen) {
		INTREQ(0x8000 | (0x80 << nr));
	    }
	    cdp->intreq2 = 0;

	    cdp->dat = cdp->nextdat;
	    if (cdp->dmaen)
		cdp->data_written = 2;
	    if (nr < 3) {
		if (cdp->dat == 0)
		    (cdp+1)->per = 65535;

		else if (cdp->dat < maxhpos/2 && currprefs.produce_sound < 3)
		    (cdp+1)->per = maxhpos/2;
		else
		    (cdp+1)->per = cdp->dat;
	    }
	}
	break;

     case 3:
	/* We come here when a 3->2 transition occurs */
	if (currprefs.produce_sound == 0)
	    cdp->per = 65535;

	cdp->evtime = cdp->per;

	if ((INTREQR() & (0x80 << nr)) && !cdp->dmaen) {
	    cdp->state = 0;
	    cdp->last_sample[2] = 0;
	    cdp->last_sample[1] = 0;
	    cdp->last_sample[0] = 0;
	    cdp->current_sample = 0;
	    break;
	} else {
	    int audav = adkcon & (1 << nr);
	    int audap = adkcon & (16 << nr);
	    int napnav = (!audav && !audap) || audav;
	    cdp->state = 2;

	    if ((cdp->intreq2 && cdp->dmaen && napnav)
		|| (napnav && !cdp->dmaen)) {
	      INTREQ(0x8000 | (0x80 << nr));
	    }
	    cdp->intreq2 = 0;

	    cdp->dat = cdp->nextdat;
            cdp->last_sample[2] = cdp->last_sample[1];
            cdp->last_sample[1] = cdp->last_sample[0];
            cdp->last_sample[0] = cdp->current_sample;
	    cdp->current_sample = (uae_s8)(cdp->dat >> 8);

	    if (cdp->dmaen && napnav)
		cdp->data_written = 2;

	    /* Volume attachment? */
	    if (audav) {
		if (nr < 3) {
		    (cdp+1)->vol = cdp->dat;
		}
	    }
	}
	break;

     default:
	cdp->state = 0;
	break;
    }
}


void audio_reset (void)
{
    memset (audio_channel, 0, sizeof audio_channel);
    audio_channel[0].per = 65535;
    audio_channel[1].per = 65535;
    audio_channel[2].per = 65535;
    audio_channel[3].per = 65535;

    last_cycles = 0;
    next_sample_evtime = sample_evtime;

    audperhack = 0;

    memset(sound_left_input, 0, sizeof(sound_left_input));
    memset(sound_right_input, 0, sizeof(sound_right_input));
    memset(sound_left_output, 0, sizeof(sound_left_output));
    memset(sound_right_output, 0, sizeof(sound_right_output));
}

static int sound_prefs_changed (void)
{
    return (changed_prefs.produce_sound != currprefs.produce_sound
	    || changed_prefs.stereo != currprefs.stereo
	    || changed_prefs.sound_freq != currprefs.sound_freq
	    || changed_prefs.sound_bits != currprefs.sound_bits);
}

void check_prefs_changed_audio (void)
{
    if (sound_available && sound_prefs_changed ()) {
	close_sound ();

	currprefs.produce_sound = changed_prefs.produce_sound;
	currprefs.stereo = changed_prefs.stereo;
	currprefs.sound_bits = changed_prefs.sound_bits;
	currprefs.sound_freq = changed_prefs.sound_freq;

	if (currprefs.produce_sound >= 2) {
	    if (init_sound ()) {
		last_cycles = cycles - 1;
		next_sample_evtime = sample_evtime;
	    } else
		if (! sound_available) {
		    fprintf (stderr, "Sound is not supported.\n");
		} else {
		    fprintf (stderr, "Sorry, can't initialize sound.\n");
		    currprefs.produce_sound = 0;
		    /* So we don't do this every frame */
		    changed_prefs.produce_sound = 0;
		}
	}
    }
}

void select_audio_interpolator(char *name)
{

  /* This is just for compatibility with the old crap */
  switch (currprefs.sound_interpol) {
  case 0:
    sample_handler = sample16s_handler;
    break;
  case 1:
    sample_handler = sample16si_linear_handler;
    break;
  case 2:
    sample_handler = sample16si_crux_handler;
    break;
  default:
    fprintf(stderr, "\nUnknown interpolator number in uaerc: %d\n", currprefs.sound_interpol);
    exit(-1);
  }

  /* This is the new system (a user should give the interpolation mode as a
     human-readable string) */
  if (name != NULL) {
    if (strcmp(name, "default") == 0) {
      sample_handler = sample16s_handler;
    } else if (strcmp(name, "rh") == 0 || strcmp(name, "linear") == 0) {
      sample_handler = sample16si_linear_handler;
    } else if (strcmp(name, "crux") == 0) {
      sample_handler = sample16si_crux_handler;
    } else if (strcmp(name, "cspline") == 0) {
      sample_handler = sample16si_cspline_handler;
    } else {
      fprintf(stderr, "\nUnknown interpolation mode: %s\n", name);
      exit(-1);
    }
  }
}

void update_audio (void)
{
    unsigned long int n_cycles;

    if (currprefs.produce_sound < 2)
	return;

    n_cycles = cycles - last_cycles;
    for (;;) {
	unsigned long int best_evtime = n_cycles + 1;
	if (audio_channel[0].state != 0 && best_evtime > audio_channel[0].evtime)
	    best_evtime = audio_channel[0].evtime;
	if (audio_channel[1].state != 0 && best_evtime > audio_channel[1].evtime)
	    best_evtime = audio_channel[1].evtime;
	if (audio_channel[2].state != 0 && best_evtime > audio_channel[2].evtime)
	    best_evtime = audio_channel[2].evtime;
	if (audio_channel[3].state != 0 && best_evtime > audio_channel[3].evtime)
	    best_evtime = audio_channel[3].evtime;
	if (best_evtime > next_sample_evtime)
	    best_evtime = next_sample_evtime;

	if (best_evtime > n_cycles)
	    break;

	next_sample_evtime -= best_evtime;
	audio_channel[0].evtime -= best_evtime;
	audio_channel[1].evtime -= best_evtime;
	audio_channel[2].evtime -= best_evtime;
	audio_channel[3].evtime -= best_evtime;
	n_cycles -= best_evtime;
	if (next_sample_evtime == 0 && currprefs.produce_sound > 1) {
	    next_sample_evtime = sample_evtime;
	    (*sample_handler) ();
	}
	if (audio_channel[0].evtime == 0 && audio_channel[0].state != 0)
	    audio_handler (0);
	if (audio_channel[1].evtime == 0 && audio_channel[1].state != 0)
	    audio_handler (1);
	if (audio_channel[2].evtime == 0 && audio_channel[2].state != 0)
	    audio_handler (2);
	if (audio_channel[3].evtime == 0 && audio_channel[3].state != 0)
	    audio_handler (3);
    }
    last_cycles = cycles - n_cycles;
}

void AUDxDAT (int nr, uae_u16 v)
{
    struct audio_channel_data *cdp = audio_channel + nr;

    update_audio ();

    cdp->dat = v;
    if (cdp->state == 0 && !(INTREQR() & (0x80 << nr))) {
	cdp->state = 2;
	INTREQ(0x8000 | (0x80 << nr));
	/* data_written = 2 ???? */
	cdp->evtime = cdp->per;
    }
}

void AUDxLCH (int nr, uae_u16 v)
{
    update_audio ();

    audio_channel[nr].lc = (audio_channel[nr].lc & 0xffff) | ((uae_u32)v << 16);
}

void AUDxLCL (int nr, uae_u16 v)
{
    update_audio ();

    audio_channel[nr].lc = (audio_channel[nr].lc & ~0xffff) | (v & 0xFFFE);
}

void AUDxPER (int nr, uae_u16 v)
{
    update_audio ();

    if (v == 0)
	v = 65535;
    else if (v < 16) {
	/* With the risk of breaking super-cool players,
	   we limit the value to 16 to save cpu time on not so powerful
	   machines. robocop customs use low values for example. */
	if (!audperhack) {
	    audperhack = 1;
	    uade_send_debug("Eagleplayer inserted %d into aud%dper.", v, nr);
	}
	v = 16;
    }
    if (v < maxhpos/2 && currprefs.produce_sound < 3)
	v = maxhpos/2;
    audio_channel[nr].per = v;
}

void AUDxLEN (int nr, uae_u16 v)
{
    update_audio ();

    audio_channel[nr].len = v;
}

void AUDxVOL (int nr, uae_u16 v)
{
    int v2 = v & 64 ? 63 : v & 63;

    update_audio ();

    audio_channel[nr].vol = v2;
}
