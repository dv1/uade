#ifndef _UADE123_H_
#define _UADE123_H_

#include <limits.h>
#include <stdio.h>

#include "playlist.h"


#define debug(fmt, args...) if (uade_verbose_mode) { fprintf(stderr, fmt, ## args); }
#define tprintf(fmt, args...) do {fprintf(uade_terminal_file ? uade_terminal_file : stdout, fmt, ## args); } while (0)

#define FILTER_MODEL_A500  1
#define FILTER_MODEL_A1200 2

extern int uade_debug_trigger;
extern int uade_force_filter;
extern int uade_filter_state;
extern int uade_ignore_player_check;
extern int uade_info_mode;
extern char *uade_interpolation_mode;
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
extern FILE *uade_terminal_file;
extern int uade_terminal_mode;
extern int uade_use_filter;
extern int uade_use_panning;
extern float uade_panning_value;
extern int uade_verbose_mode;


void print_action_keys(void);
void set_filter_on(const char *model);
int test_song_end_trigger(void);

#endif
