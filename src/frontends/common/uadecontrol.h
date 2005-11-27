#ifndef _UADE_CONTROL_
#define _UADE_CONTROL_

enum {
  UADECORE_INIT_OK = 0,
  UADECORE_INIT_ERROR,
  UADECORE_CANT_PLAY
};

void uade_change_subsong(int subsong);
void uade_send_filter_command(int filter_type, int filter_state, int force_filter);
void uade_send_interpolation_command(const char *mode);
void uade_set_subsong(int subsong);
int uade_song_initialization(const char *scorename,
			     const char *playername,
			     const char *modulename);
void uade_spawn(pid_t *uadepid, const char *uadename, const char *configname,
		int debug_mode);

#endif
