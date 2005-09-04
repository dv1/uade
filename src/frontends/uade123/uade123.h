#ifndef _UADE123_H_
#define _UADE123_H_

#include <limits.h>

#include "playlist.h"


#define debug(fmt, args...) if (uade_verbose_mode) { fprintf(stderr, fmt, ## args); }

extern int uade_debug_trigger;
extern int uade_ignore_player_check;
extern int uade_info_mode;
extern double uade_jump_pos;
extern int uade_no_output;
extern int uade_one_subsong_per_file;
extern char uade_output_file_format[16];
extern char uade_output_file_name[PATH_MAX];
extern struct playlist uade_playlist;
extern int uade_recursivemode;
extern int uade_song_end_trigger;
extern int uade_silence_timeout;
extern int uade_subsong_timeout;
extern int uade_timeout;
extern int uade_terminated;
extern int uade_terminal_mode;
extern int uade_use_panning;
extern float uade_panning_value;
extern int uade_verbose_mode;


int test_song_end_trigger(void);

#endif
