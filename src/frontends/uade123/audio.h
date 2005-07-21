#ifndef _UADE123_AUDIO_H_
#define _UADE123_AUDIO_H_

void audio_close(void);
int audio_init(void);
int audio_play(char *samples, int bytes);

extern int uade_bytes_per_sample;
extern int uade_sample_bytes_per_second;


#endif
