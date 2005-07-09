#ifndef _UADE_MAIN_H_
#define _UADE_MAIN_H_

#define UADE_PATH_MAX (1024)

struct uade_song {
  char playername[UADE_PATH_MAX];       /* filename of eagleplayer */
  char modulename[UADE_PATH_MAX];       /* filename of song */
  char scorename[UADE_PATH_MAX];        /* filename of score file */

  int set_subsong;
  int subsong;
  int force_by_default;
  int use_ntsc;
  int song_end_possible;
  int use_filter;

  int timeout;          /* default timeout infinite */
  int subsong_timeout;  /* default per subsong timeout infinite */
  int silence_timeout;  /* default silence timeout */

  int min_subsong;
  int max_subsong;
  int cur_subsong;
};


void uade_change_subsong(int subs);
void uade_get_amiga_message(void);
void uade_option(int, char**); /* handles command line parameters */
void uade_receive_control(void);
void uade_reset(void);
void uade_send_amiga_message(int msgtype);
void uade_set_automatic_song_end(int song_end_possible);
void uade_set_ntsc(int usentsc);
void uade_song_end(char *reason, int kill_it);
void uade_swap_buffer_bytes(void *data, int bytes);
void uade_test_sound_block(void *buf, int size); /* for silence detection */
void uade_vsync_handler(void);
void uade_check_sound_buffers(int bytes);

void uade_print_help(int problemcode);

extern int uade_debug;
extern int uade_local_sound;
extern int uade_read_size;
extern int uade_reboot;
extern int uade_swap_output_bytes;
extern int uade_time_critical;

#endif
