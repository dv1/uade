#ifndef _UADE_CONTROL_
#define _UADE_CONTROL_

enum {
  UADECORE_INIT_OK = 0,
  UADECORE_INIT_ERROR,
  UADECORE_CANT_PLAY
};

int uade_song_initialization(const char *scorename,
			     const char *playername,
			     const char *modulename);

void uade_spawn(pid_t *uadepid, const char *uadename, int debug_mode);

#endif
