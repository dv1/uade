#ifndef _UADE123_CONFIG_H_
#define _UADE123_CONFIG_H_

#include <uadeconf.h>

void config_set_interpolation_mode(const char *value);
void config_set_panning(const char *value);
void config_set_silence_timeout(const char *value);
void config_set_subsong_timeout(const char *value);
void config_set_timeout(const char *value);
void post_config(struct uade_config *uc);

#endif
