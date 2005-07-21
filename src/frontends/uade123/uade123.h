#ifndef _UADE123_H_
#define _UADE123_H_

#include <limits.h>

#define debug(fmt, args...) if (verbose_mode) { fprintf(stderr, fmt, ## args); }

extern int bytes_per_sample;
extern int debug_trigger;
extern int one_subsong_per_file;
extern char output_file_format[16];
extern char output_file_name[PATH_MAX];
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
