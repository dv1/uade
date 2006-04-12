#ifndef _UADE_FRONTEND_CONFIG_H_
#define _UADE_FRONTEND_CONFIG_H_

#include <amigafilter.h>
#include <eagleplayer.h>
#include <effects.h>


struct uade_config {
  int action_keys;
  int action_keys_set;
  int always_ends;
  int always_ends_set;
  int buffer_time;
  int buffer_time_set;
  int filter_type;
  int filter_type_set;
  int led_forced;
  int led_forced_set;
  int led_state;
  int gain_enable;
  int gain_enable_set;
  float gain; /* should be removed of uade_effect integrated */
  int headphones;
  int headphones_set;
  int ignore_player_check;
  int ignore_player_check_set;
  char *interpolator;
  int interpolator_set;
  int no_filter;
  int no_filter_set;
  int no_song_end;
  int no_song_end_set;
  int one_subsong;
  int one_subsong_set;
  int panning_enable;
  int panning_enable_set;
  float panning; /* should be removed */
  int random_play;
  int random_play_set;
  int recursive_mode;
  int recursive_mode_set;
  int silence_timeout;
  int silence_timeout_set;
  char *song_title;
  int song_title_set;
  int speed_hack;
  int speed_hack_set;
  int subsong_timeout;
  int subsong_timeout_set;
  int timeout;
  int timeout_set;
  int use_ntsc;
  int use_ntsc_set;
};


void uade_config_set_defaults(struct uade_config *uc);
double uade_convert_to_double(const char *value, double def,
			      double low, double high, const char *type);
int uade_load_config(struct uade_config *uc, const char *filename);
void uade_merge_configs(struct uade_config *ucd, const struct uade_config *ucs);
void uade_set_config_effects(struct uade_effect *effects, struct uade_config *uc);
int uade_set_config_option(struct uade_config *uc, const char *key, const char *value);
void uade_set_ep_attributes(struct uade_config *uc, struct eagleplayer *ep);
void uade_set_filter_type(struct uade_config *uc, const char *value);
void uade_set_song_attributes(struct uade_config *uc, struct uade_effect *ue,
			      struct uade_song *us);

#endif
