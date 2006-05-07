 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Sound emulation stuff
  *
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  */

#ifndef _UADE_AUDIO_H_
#define _UADE_AUDIO_H_

#define AUDIO_DEBUG 0
#define SINC_QUEUE_MAX_AGE 4096
#define SINC_QUEUE_LENGTH 96

typedef struct {
    int age, output;
} sinc_queue_t;

extern struct audio_channel_data {
    unsigned long adk_mask;
    unsigned long evtime;
    unsigned char dmaen, intreq2, data_written;
    uaecptr lc, pt;

    int state, wper, wlen;
    int current_sample;
    int sample_accum, sample_accum_time;
    sinc_queue_t sinc_queue[SINC_QUEUE_LENGTH];
    int sinc_queue_length;
    int vol;
    uae_u16 dat, nextdat, per, len;    

    /* Debug variables */
    uaecptr ptend, nextdatpt, nextdatptend, datpt, datptend;
} audio_channel[4];

extern void AUDxDAT (int nr, uae_u16 value);
extern void AUDxVOL (int nr, uae_u16 value);
extern void AUDxPER (int nr, uae_u16 value);
extern void AUDxLCH (int nr, uae_u16 value);
extern void AUDxLCL (int nr, uae_u16 value);
extern void AUDxLEN (int nr, uae_u16 value);

void audio_reset (void);
void audio_set_filter(int filter_type, int filter_force);
void audio_set_rate (int rate);
void audio_set_resampler(char *name);
void update_audio (void);

#endif
