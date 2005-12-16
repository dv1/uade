#ifndef _UADE_FRONTEND_CONFIG_H_
#define _UADE_FRONTEND_CONFIG_H_

#include <amigafilter.h>

struct uade_config {
  int action_keys;
  int filter;
  int force_led;
  float gain;
  int headphones;
  int ignore_player_check;
  char *interpolator;
  int no_filter;
  int one_subsong;
  float panning;
  int random_play;
  int recursive_mode;
  int silence_timeout;
  int subsong_timeout;
  int timeout;
};

int uade_get_filter_type(const char *value);
int uade_get_silence_timeout(const char *value);
int uade_get_subsong_timeout(const char *value);
int uade_get_timeout(const char *value);
double uade_convert_to_double(const char *value,
			      double def,
			      double low, double high, const char *type);
int uade_load_config(struct uade_config *uc, const char *filename);
void uade_post_config(struct uade_config *uc);

#endif
