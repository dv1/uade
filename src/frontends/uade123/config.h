#ifndef _UADE123_CONFIG_H_
#define _UADE123_CONFIG_H_

void config_set_panning(const char *value);
void config_set_silence_timeout(const char *value);
void config_set_subsong_timeout(const char *value);
void config_set_timeout(const char *value);
int load_config(const char *filename);

#endif
