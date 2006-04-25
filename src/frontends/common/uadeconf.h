#ifndef _UADE_FRONTEND_CONFIG_H_
#define _UADE_FRONTEND_CONFIG_H_

#include <uadeconfstructure.h>
#include <eagleplayer.h>
#include <effects.h>

void uade_config_set_defaults(struct uade_config *uc);
double uade_convert_to_double(const char *value, double def,
			      double low, double high, const char *type);
int uade_load_config(struct uade_config *uc, const char *filename);
void uade_merge_configs(struct uade_config *ucd, const struct uade_config *ucs);
int uade_parse_subsongs(int **subsongs, char *option);
void uade_set_config_effects(struct uade_effect *effects,
			     const struct uade_config *uc);
void uade_set_config_option(struct uade_config *uc, enum uade_option opt, const char *value);
void uade_set_ep_attributes(struct uade_config *uc, struct eagleplayer *ep);
void uade_set_filter_type(struct uade_config *uc, const char *value);
void uade_set_song_attributes(struct uade_config *uc, struct uade_effect *ue,
			      struct uade_song *us);

#endif
