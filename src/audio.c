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

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "gensound.h"
#include "sd-sound.h"
#include "events.h"
#include "cia.h"
#include "audio.h"

#include "uade.h"

/* #define AUDIO_HW_DEBUG */

#undef BENCHMARK_AUDIO

#ifdef BENCHMARK_AUDIO

#define BEGIN_BENCH frame_time_t audbench = read_processor_time ();
#define END_BENCH sh_time += read_processor_time () - audbench; sh_count++;
static frame_time_t sh_time = 0;
unsigned long sh_count = 0;

#else

#define BEGIN_BENCH
#define END_BENCH

#endif

struct audio_channel_data audio_channel[4];
int sound_available = 0;
int sound_table[64][256];
void (*sample_handler) (void);
unsigned long int sample_evtime;
static unsigned long last_cycles, next_sample_evtime;

unsigned long sample_evtime;

int sound_use_filter = 0;
static float sound_left_input[6];
static float sound_left_output[6];
static float sound_right_input[6];
static float sound_right_output[6];

/* apply filter emulation (IIR)
   y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2,
   where
     b0 = 0.0354860792783
     b1 = 0.0709721585565
     b2 = 0.0354860792783
     a1 = -1.43583028974
     a2 = 0.577774606849
   and x0 is the current input sample, x1 the input 1 sample ago, x2 the
   input 2 samples ago, y1 the output of filter 1 sample ago, and y2 the
   output 2 samples ago.
*/

static int filter(int data, float *input, float *output, int down, int up)
{
  int o;
  float s;
  const int scale = -down;
  input[2] = input[1];
  input[1] = input[0];
  input[0] = ((float) data) / scale;

  if (!gui_ledstate) {
    s = 0.0;
    o = data;
  } else {
    s = 0.0354860792783 * input[0] + 0.0709721585565 * input[1] + 0.0354860792783 * input[2];
    s -= -1.43583028974 * output[0] + 0.577774606849 * output[1];
    s *= 0.99; /* to avoid overruns */
    o = s * up;
  }
  output[1] = output[0];
  output[0] = s;
  if (o > up) {
    o = up;
  } else if (o < down) {
    o = down;
  }
  return o;
}


void init_sound_table16 (void)
{
    int i,j;

    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[j][i] = j * (uae_s8)i * (currprefs.stereo ? 2 : 1);
}

void init_sound_table8 (void)
{
    int i,j;

    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[j][i] = (j * (uae_s8)i * (currprefs.stereo ? 2 : 1)) / 256;
}

#define MULTIPLICATION_PROFITABLE

#ifdef MULTIPLICATION_PROFITABLE
typedef uae_s8 sample8_t;
#define DO_CHANNEL_1(v, c) do { (v) *= audio_channel[c].vol; } while (0)
#define SBASEVAL8(logn) ((logn) == 1 ? SOUND8_BASE_VAL << 7 : SOUND8_BASE_VAL << 8)
#define SBASEVAL16(logn) ((logn) == 1 ? SOUND16_BASE_VAL >> 1 : SOUND16_BASE_VAL)
#define FINISH_DATA(b,logn) do { if (14 - (b) + (logn) > 0) data >>= 14 - (b) + (logn); else data <<= (b) - 14 - (logn); } while (0);
#else
typedef uae_u8 sample8_t;
#define DO_CHANNEL_1(v, c) do { (v) = audio_channel[c].voltbl[(v)]; } while (0)
#define SBASEVAL8(logn) SOUND8_BASE_VAL
#define SBASEVAL16(logn) SOUND16_BASE_VAL
#define FINISH_DATA(b,logn)
#endif

#define DO_CHANNEL(v, c) do { (v) &= audio_channel[c].adk_mask; data += v; } while (0);

#define FILTER_LEFT(data, n) filter(data, sound_left_input, sound_left_output, -128*64*n, 127*64*n)
#define FILTER_RIGHT(data, n) filter(data, sound_right_input, sound_right_output, -128*64*n, 127*64*n)


void sample16s_handler (void)
{
    BEGIN_BENCH

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;

    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);

    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    
    data0 += data3;
    data1 += data2;

    if (sound_use_filter) {
      data0 = FILTER_LEFT(data0, 2);
      data1 = FILTER_RIGHT(data1, 2);
    }

    {
	uae_u32 data = SBASEVAL16(1) + data0;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_RIGHT (data);
    }

    {
	uae_u32 data = SBASEVAL16(1) + data1;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_LEFT (data);
    }
    
    END_BENCH
    
    check_sound_buffers ();
}

void sample16si_crux_handler (void)
{
    unsigned long delta, ratio;

    BEGIN_BENCH

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    uae_u32 data0p = audio_channel[0].last_sample;
    uae_u32 data1p = audio_channel[1].last_sample;
    uae_u32 data2p = audio_channel[2].last_sample;
    uae_u32 data3p = audio_channel[3].last_sample;

    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    DO_CHANNEL_1 (data0p, 0);
    DO_CHANNEL_1 (data1p, 1);
    DO_CHANNEL_1 (data2p, 2);
    DO_CHANNEL_1 (data3p, 3);

    data0 &= audio_channel[0].adk_mask;
    data0p &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data1p &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data2p &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data3p &= audio_channel[3].adk_mask;

    {    
	struct audio_channel_data *cdp;
	int ratio, ratio1;
#define INTERVAL (sample_evtime * 3)
	cdp = audio_channel + 0;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data0 = (data0 * ratio + data0p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 1;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data1 = (data1 * ratio + data1p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 2;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data2 = (data2 * ratio + data2p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 3;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data3 = (data3 * ratio + data3p * (4096 - ratio)) >> 12;
    }
    data1 += data2;
    data0 += data3;

    if (sound_use_filter) {
      data0 = FILTER_LEFT(data0, 2);
      data1 = FILTER_RIGHT(data1, 2);
    }

    {
	uae_u32 data = SBASEVAL16 (1) + data0;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_RIGHT (data);
    }

    {
	uae_u32 data = SBASEVAL16 (1) + data1;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_LEFT (data);
    }
    
    END_BENCH
    
    check_sound_buffers ();
}

void sample16si_rh_handler (void)
{
    unsigned long delta, ratio;

    BEGIN_BENCH

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    uae_u32 data0p = audio_channel[0].last_sample;
    uae_u32 data1p = audio_channel[1].last_sample;
    uae_u32 data2p = audio_channel[2].last_sample;
    uae_u32 data3p = audio_channel[3].last_sample;

    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    DO_CHANNEL_1 (data0p, 0);
    DO_CHANNEL_1 (data1p, 1);
    DO_CHANNEL_1 (data2p, 2);
    DO_CHANNEL_1 (data3p, 3);

    data0 &= audio_channel[0].adk_mask;
    data0p &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data1p &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data2p &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data3p &= audio_channel[3].adk_mask;

    /* linear interpolation and summing up... */
    delta = audio_channel[0].per;
    ratio = ((audio_channel[0].evtime % delta) << 8) / delta;
    data0 = (data0 * (256 - ratio) + data0p * ratio) >> 8;
    delta = audio_channel[1].per;
    ratio = ((audio_channel[1].evtime % delta) << 8) / delta;
    data1 = (data1 * (256 - ratio) + data1p * ratio) >> 8;
    delta = audio_channel[2].per;
    ratio = ((audio_channel[2].evtime % delta) << 8) / delta;
    data1 += (data2 * (256 - ratio) + data2p * ratio) >> 8;
    delta = audio_channel[3].per;
    ratio = ((audio_channel[3].evtime % delta) << 8) / delta;
    data0 += (data3 * (256 - ratio) + data3p * ratio) >> 8;

    if (sound_use_filter) {
      data0 = FILTER_LEFT(data0, 2);
      data1 = FILTER_RIGHT(data1, 2);
    }

    {
	uae_u32 data = SBASEVAL16 (1) + data0;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_RIGHT (data);
    }

    {
	uae_u32 data = SBASEVAL16 (1) + data1;
	FINISH_DATA (16, 1);
	PUT_SOUND_WORD_LEFT (data);
    }
    
    END_BENCH
    
    check_sound_buffers ();
}


static uae_u8 int2ulaw (int ch)
{
    int mask;

    if (ch < 0) {
      ch = -ch;
      mask = 0x7f;
    }
    else {
      mask = 0xff;
    }

    if (ch < 32) {
	ch = 0xF0 | ( 15 - (ch/2) );
    } else if (ch < 96) {
	ch = 0xE0 | ( 15 - (ch-32)/4 );
    } else if (ch < 224) {
	ch = 0xD0 | ( 15 - (ch-96)/8 );
    } else if (ch < 480) {
	ch = 0xC0 | ( 15 - (ch-224)/16 );
    } else if (ch < 992 ) {
	ch = 0xB0 | ( 15 - (ch-480)/32 );
    } else if (ch < 2016) {
	ch = 0xA0 | ( 15 - (ch-992)/64 );
    } else if (ch < 4064) {
	ch = 0x90 | ( 15 - (ch-2016)/128 );
    } else if (ch < 8160) {
	ch = 0x80 | ( 15 - (ch-4064)/256 );
    } else {
	ch = 0x80;
    }
    return (uae_u8)(mask & ch);
}

void sample_ulaw_handler (void)
{
    int nr;
    uae_u32 data = 0;

    for (nr = 0; nr < 4; nr++) {
	if (!(adkcon & (0x11 << nr))) {
	    uae_u32 d = audio_channel[nr].current_sample;
	    DO_CHANNEL_1 (d, nr);
	    data += d;
	}
    }
    PUT_SOUND_BYTE (int2ulaw (data));
    check_sound_buffers ();
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
	cdp->last_sample = cdp->current_sample;
	cdp->current_sample = (sample8_t)(cdp->dat >> 8);

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

	cdp->last_sample = cdp->current_sample;
	cdp->current_sample = (sample8_t)(cdp->dat & 0xFF);
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
	    cdp->last_sample = 0;
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
	    cdp->last_sample = cdp->current_sample;
	    cdp->current_sample = (sample8_t)(cdp->dat >> 8);

	    if (cdp->dmaen && napnav)
		cdp->data_written = 2;

	    /* Volume attachment? */
	    if (audav) {
		if (nr < 3) {
		    (cdp+1)->vol = cdp->dat;
#ifndef MULTIPLICATION_PROFITABLE
		    (cdp+1)->voltbl = sound_table[cdp->dat];
#endif
		}
	    }
	}
	break;

     default:
	cdp->state = 0;
	break;
    }
}

void aud0_handler (void)
{
    audio_handler (0);
}
void aud1_handler (void)
{
    audio_handler (1);
}
void aud2_handler (void)
{
    audio_handler (2);
}
void aud3_handler (void)
{
    audio_handler (3);
}

void audio_reset (void)
{
    memset (audio_channel, 0, sizeof audio_channel);
    audio_channel[0].per = 65535;
    audio_channel[1].per = 65535;
    audio_channel[2].per = 65535;
    audio_channel[3].per = 65535;
    audio_channel[0].voltbl = sound_table[0];
    audio_channel[1].voltbl = sound_table[0];
    audio_channel[2].voltbl = sound_table[0];
    audio_channel[3].voltbl = sound_table[0];

    last_cycles = 0;
    next_sample_evtime = sample_evtime;

    memset(sound_left_input, 0, sizeof(sound_left_input));
    memset(sound_right_input, 0, sizeof(sound_right_input));
    memset(sound_left_output, 0, sizeof(sound_left_output));
    memset(sound_left_output, 0, sizeof(sound_left_output));
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
    /* Select the right interpolation method.  */
    if (sample_handler == sample16s_handler
	|| sample_handler == sample16si_crux_handler
	|| sample_handler == sample16si_rh_handler)
      sample_handler = (currprefs.sound_interpol == 0 ? sample16s_handler
			  : currprefs.sound_interpol == 1 ? sample16si_rh_handler
			  : sample16si_crux_handler);
}

void update_audio (void)
{
    unsigned long int n_cycles;

    if (currprefs.produce_sound < 2)
	return;

    n_cycles = cycles - last_cycles;
    for (;;) {
	int best = -1;
	unsigned long int best_evtime = n_cycles + 1;
	if (audio_channel[0].state != 0 && best_evtime > audio_channel[0].evtime)
	    best = 0, best_evtime = audio_channel[0].evtime;
	if (audio_channel[1].state != 0 && best_evtime > audio_channel[1].evtime)
	    best = 1, best_evtime = audio_channel[1].evtime;
	if (audio_channel[2].state != 0 && best_evtime > audio_channel[2].evtime)
	    best = 2, best_evtime = audio_channel[2].evtime;
	if (audio_channel[3].state != 0 && best_evtime > audio_channel[3].evtime)
	    best = 3, best_evtime = audio_channel[3].evtime;
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

#ifdef AUDIO_HW_DEBUG
    {
      struct timeval tv;
      gettimeofday(&tv, 0);
      printf("AUD%dDAT: %d %.6d\n", nr, tv.tv_sec, tv.tv_usec);
    }
#endif
}

void AUDxLCH (int nr, uae_u16 v)
{
    update_audio ();

    audio_channel[nr].lc = (audio_channel[nr].lc & 0xffff) | ((uae_u32)v << 16);

#ifdef AUDIO_HW_DEBUG
    {
      struct timeval tv;
      gettimeofday(&tv, 0);
      printf("AUD%dLCH: %d %.6d\n", nr, tv.tv_sec, tv.tv_usec);
    }
#endif
}

void AUDxLCL (int nr, uae_u16 v)
{
    update_audio ();

    audio_channel[nr].lc = (audio_channel[nr].lc & ~0xffff) | (v & 0xFFFE);

#ifdef AUDIO_HW_DEBUG
    {
      struct timeval tv;
      gettimeofday(&tv, 0);
      printf("AUD%dLCL: %d %.6d\n", nr, tv.tv_sec, tv.tv_usec);
    }
#endif
}

void AUDxPER (int nr, uae_u16 v)
{
  static int audperhack = 0;
  update_audio ();

  if (v == 0)
    v = 65535;

  if (v < 16) {
    /* with the risk of breaking super-cool players (that i'm not aware of)
       we limit the value to 16 to save cpu time on not so powerful
       machines. robocop customs use low values for example. */
    if (!audperhack) {
      audperhack = 1;
      fprintf(stderr, "uade: eagleplayer probably used audperhack (inserted %d into aud%dper)\n", v, nr);
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
#ifndef MULTIPLICATION_PROFITABLE
    audio_channel[nr].voltbl = sound_table[v2];
#endif

#ifdef AUDIO_HW_DEBUG
    {
      struct timeval tv;
      gettimeofday(&tv, 0);
      printf("AUD%dVOL: %d @ %d %.6d\n", nr, v, tv.tv_sec, tv.tv_usec);
    }
#endif
}

void dump_audio_bench (void)
{
#ifdef BENCHMARK_AUDIO
    printf ("Average cycles per sample handler: %f\n", ((double)sh_time / sh_count));
#endif
}

