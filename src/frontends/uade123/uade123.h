#ifndef _UADE123_H_
#define _UADE123_H_

#include <ao/ao.h>

#define debug(fmt, args...) if (verbose_mode) { fprintf(stderr, fmt, ## args); }

extern int bytes_per_sample;
extern int debug_trigger;
extern ao_device *libao_device;
extern int one_subsong_per_file;
extern int sample_bytes_per_second;
extern int song_end_trigger;
extern int subsong_timeout_value;
extern int timeout_value;
extern int uadeterminated;
extern int use_panning;
extern float panning_value;
extern int verbose_mode;


int test_song_end_trigger(void);

#endif
