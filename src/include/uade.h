#ifndef _UADE_MAIN_H_
#define _UADE_MAIN_H_

void uade_change_subsong(int subs);
void uade_get_amiga_message(void);
void uade_option(int, char**); /* handles command line parameters */
void uade_prerun(void);
void uade_send_amiga_message(int msgtype);
void uade_set_automatic_song_end(int song_end_possible);
void uade_set_ntsc(int usentsc);
void uade_song_end(char *reason, int kill_it);
void uade_swap_buffer_bytes(void *data, int bytes);
void uade_test_sound_block(void *buf, int size); /* for silence detection */
void uade_vsync_handler(void);
int uade_check_sound_buffers(void *sndbuffer, int sndbufsize, int bytes_per_sample);

void uade_print_help(int problemcode);

extern int uade_swap_output_bytes;
extern int uade_reboot;
extern int uade_time_critical;
extern int uade_debug;

#endif
