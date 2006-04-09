/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <limits.h>
#include <unistd.h>

#include <strlrep.h>

#include "uadeconf.h"


static char config_filename[PATH_MAX];


static int uade_set_silence_timeout(struct uade_config *uc, const char *value);
static int uade_set_subsong_timeout(struct uade_config *uc, const char *value);
static int uade_set_timeout(struct uade_config *uc, const char *value);


static char *nextspace(const char *foo)
{
  while (foo[0] != 0 && !isspace(foo[0]))
    foo++;
  if (foo[0] == 0)
    return NULL;
  return (char *) foo;
}


static char *nextnonspace(const char *foo)
{
  while (foo[0] != 0 && isspace(foo[0]))
    foo++;
  if (foo[0] == 0)
    return NULL;
  return (char *) foo;
}


void uade_config_set_defaults(struct uade_config *uc)
{
  memset(uc, 0, sizeof(*uc));
  uc->action_keys = 1;
  uc->action_keys_set = 1;
  uade_set_filter_type(uc, NULL);
  uc->gain = 1.0;
  uc->panning = 0.7;
  uc->silence_timeout = 20;
  uc->silence_timeout_set = 1;
  uc->subsong_timeout = 512;
  uc->subsong_timeout_set = 1;
  uc->timeout = -1;
  uc->timeout_set = 1;
}


double uade_convert_to_double(const char *value, double def, double low, double high, const char *type)
{
  char *endptr;
  double v;
  if (value == NULL) {
    fprintf(stderr, "Must have a parameter value for %s in config file %s\n", config_filename, type);
    return def;
  }
  v = strtod(value, &endptr);
  if (*endptr != 0 || v < low || v > high) {
    fprintf(stderr, "Invalid %s value: %s\n", type, value);
    v = def;
  }
  return v;
}


int uade_load_config(struct uade_config *uc, const char *filename)
{
  char line[256];
  FILE *f;
  char *key;
  char *value;
  int linenumber = 0;

  if ((f = fopen(filename, "r")) == NULL)
    return 0;

  strlcpy(config_filename, filename, sizeof config_filename);

  while (fgets(line, sizeof(line), f) != NULL) {
    linenumber++;
    if (line[strlen(line) - 1] == '\n')
      line[strlen(line) - 1] = 0;
    if (line[0] == 0)
      continue;
    if (line[0] == '#')
      continue;
    key = line;
    value = nextspace(key);
    if (value != NULL) {
      *value = 0;
      value = nextnonspace(value + 1);
    }
    if (uade_set_config_option(uc, key, value)) {
      fprintf(stderr, "Unknown config key in %s on line %d: %s\n", filename, linenumber, key);
    }
  }

  fclose(f);
  return 1;
}


void uade_merge_configs(struct uade_config *ucd, const struct uade_config *ucs)
{
  #define MERGE_OPTION(y) do { if (ucs->y##_set) ucd->y = ucs->y; } while (0)
  MERGE_OPTION(action_keys);
  MERGE_OPTION(always_ends);
  MERGE_OPTION(buffer_time);
  MERGE_OPTION(filter_type);
  MERGE_OPTION(led_forced);
  MERGE_OPTION(gain_enable);
  MERGE_OPTION(headphones);
  MERGE_OPTION(ignore_player_check);
  MERGE_OPTION(interpolator);
  MERGE_OPTION(no_filter);
  MERGE_OPTION(no_song_end);
  MERGE_OPTION(one_subsong);
  MERGE_OPTION(panning_enable);
  MERGE_OPTION(random_play);
  MERGE_OPTION(recursive_mode);
  MERGE_OPTION(silence_timeout);
  MERGE_OPTION(speed_hack);
  MERGE_OPTION(subsong_timeout);
  MERGE_OPTION(timeout);
  MERGE_OPTION(use_ntsc);
}


void uade_set_config_effects(struct uade_effect *effects, struct uade_config *uc)
{
  if (uc->gain_enable) {
    uade_effect_gain_set_amount(effects, uc->gain);
    uade_effect_enable(effects, UADE_EFFECT_GAIN);
  }

  if (uc->headphones)
    uade_effect_enable(effects, UADE_EFFECT_HEADPHONES);

  if (uc->panning_enable) {
    uade_effect_pan_set_amount(effects, uc->panning);
    uade_effect_enable(effects, UADE_EFFECT_PAN);
  }
}


int uade_set_config_option(struct uade_config *uc, const char *key,
			   const char *value)
{
  char *endptr;
  if (strncmp(key, "action_keys", 6) == 0) {
    if (value != NULL) {
      uc->action_keys_set = 1;
      if (strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) {
	uc->action_keys = 1;
      } else if (strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) {
	uc->action_keys = 0;
      } else {
	fprintf(stderr, "uade.conf: Unknown setting for action keys: %s\n", value);
      }
    }
  } else if (strncmp(key, "buffer_time", 6) == 0) {
    uc->buffer_time_set = 1;
    uc->buffer_time = strtol(value, &endptr, 10);
    if (uc->buffer_time <= 0 || *endptr != 0) {
      fprintf(stderr, "Invalid buffer_time: %s\n", value);
      uc->buffer_time = 0;
    }
  } else if (strncmp(key, "filter", 6) == 0) {
    uade_set_filter_type(uc, value);
    uc->no_filter_set = 1;
    uc->no_filter = 0;
  } else if (strncmp(key, "force_led", 9) == 0) {
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 0;
    if (strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) {
    } else if (strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) {
      uc->led_state = 1;
    } else {
      fprintf(stderr, "Unknown force led argument: %s\n", value);
    }
  } else if (strncmp(key, "force_led_off", 12) == 0) {
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 0;
  } else if (strncmp(key, "force_led_on", 12) == 0) {
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 1;
  } else if (strcmp(key, "gain") == 0) {
    uc->gain_enable_set = 1;
    uc->gain_enable = 1;
    uc->gain = uade_convert_to_double(value, 1.0, 0.0, 128.0, "gain");
  } else if (strncmp(key, "headphones", 4) == 0) {
    uc->headphones_set = 1;
    uc->headphones = 1;
  } else if (strncmp(key, "ignore_player_check", 6) == 0) {
    uc->ignore_player_check_set = 1;
    uc->ignore_player_check = 1;
  } else if (strncmp(key, "interpolator", 5) == 0) {
    if (value == NULL) {
      fprintf(stderr, "uade.conf: No interpolator given.\n");
    } else {
      uc->interpolator_set = 1;
      uc->interpolator = strdup(value);
    }
  } else if (strncmp(key, "no_filter", 9) == 0) {
    uc->no_filter_set = 1;
    uc->no_filter = 1;
  } else if (strncmp(key, "no_song_end", 4) == 0) {
    uc->no_song_end = 1;
    uc->no_song_end_set = 1;
  } else if (strcmp(key, "ntsc") == 0) {
    uc->use_ntsc_set = 1;
    uc->use_ntsc = 1;
  } else if (strncmp(key, "one_subsong", 3) == 0) {
    uc->one_subsong_set = 1;
    uc->one_subsong = 1;
  } else if (strcmp(key, "pal") == 0) {
    uc->use_ntsc_set = 1;
    uc->use_ntsc = 0;
  } else if (strncmp(key, "panning_value", 3) == 0) {
    uc->panning_enable_set = 1;
    uc->panning_enable = 1;
    uc->panning = uade_convert_to_double(value, 0.0, 0.0, 2.0, "panning");
  } else if (strncmp(key, "random_play", 6) == 0) {
    uc->random_play_set = 1;
    uc->random_play = 1;
  } else if (strncmp(key, "recursive_mode", 9) == 0) {
    uc->recursive_mode_set = 1;
    uc->recursive_mode = 1;
  } else if (strncmp(key, "silence_timeout_value", 7) == 0) {
    uade_set_silence_timeout(uc, value);
  } else if (strncmp(key, "subsong_timeout_value", 7) == 0) {
    uade_set_subsong_timeout(uc, value);
  } else if (strncmp(key, "timeout_value", 7) == 0) {
    uade_set_timeout(uc, value);
  } else {
    fprintf(stderr, "Unknown option: %s\n", key);
    return 1;
  }
  return 0;
}


void uade_set_ep_attributes(struct uade_config *uc, struct eagleplayer *ep)
{
  if (ep->attributes & EP_ALWAYS_ENDS) {
    uc->always_ends_set = 1;
    uc->always_ends = 1;
  }

  if (ep->attributes & EP_A500) {
    uc->filter_type_set = 1;
    uc->filter_type = FILTER_MODEL_A500;
  }

  if (ep->attributes & EP_A1200) {
    uc->filter_type_set = 1;
    uc->filter_type = FILTER_MODEL_A1200;
  }

  if (ep->attributes & EP_SPEED_HACK) {
    uc->speed_hack_set = 1;
    uc->speed_hack = 1;
  }
}


void uade_set_filter_type(struct uade_config *uc, const char *model)
{
  uc->filter_type_set = 1;
  uc->filter_type = FILTER_MODEL_A500E;

  if (model == NULL)
    return;

  if (strcasecmp(model, "a500") == 0) {
    uc->filter_type = FILTER_MODEL_A500;
  } else if (strcasecmp(model, "a1200") == 0) {
    uc->filter_type = FILTER_MODEL_A1200;
  } else if (strcasecmp(model, "a500e") == 0) {
    uc->filter_type = FILTER_MODEL_A500E;
  } else if (strcasecmp(model, "a1200e") == 0) {
    uc->filter_type = FILTER_MODEL_A1200E;
  } else {
    fprintf(stderr, "Unknown filter model: %s\n", model);
  }
}


static int uade_set_silence_timeout(struct uade_config *uc, const char *value)
{
  char *endptr;
  int t;
  if (value == NULL) {
    fprintf(stderr, "Must have a parameter value for silence timeout in config file %s\n", config_filename);
    return -1;
  }
  t = strtol(value, &endptr, 10);
  if (*endptr != 0 || t < -1) {
    fprintf(stderr, "Invalid silence timeout value: %s\n", value);
    return -1;
  }
  uc->silence_timeout = t;
  uc->silence_timeout_set = 1;
  return 0;
}


void uade_set_song_attributes(struct uade_config *uc, struct uade_effect *ue,
			      struct uade_song *us)
{
  if (us->flags & ES_A500) {
    uc->filter_type_set = 1;
    uc->filter_type = FILTER_MODEL_A500;
  }
  if (us->flags & ES_A1200) {
    uc->filter_type_set = 1;
    uc->filter_type = FILTER_MODEL_A1200;
  }
  if (us->flags & ES_LED_OFF) {
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 0;
  }
  if (us->flags & ES_LED_ON) {
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 1;
  }

  if (us->flags & ES_NO_HEADPHONES) {
    uc->headphones_set = 1;
    uc->headphones = 0;
    uade_effect_disable(ue, UADE_EFFECT_HEADPHONES);
  }
  if (us->flags & ES_NO_PANNING) {
    uc->panning_enable_set = 1;
    uc->panning_enable = 0;
    uade_effect_disable(ue, UADE_EFFECT_PAN);
  }
  if (us->flags & ES_NO_POSTPROCESSING)
    uade_effect_disable(ue, UADE_EFFECT_ALLOW);

  if (us->flags & ES_NTSC) {
    uc->use_ntsc_set = 1;
    uc->use_ntsc = 1;
  }
  if (us->subsongs)
    fprintf(stderr, "Subsongs not implemented.\n");
}


static int uade_set_subsong_timeout(struct uade_config *uc, const char *value)
{
  char *endptr;
  int t;
  if (value == NULL) {
    fprintf(stderr, "Must have a parameter value for subsong timeout in config file %s\n", config_filename);
    return -1;
  }
  t = strtol(value, &endptr, 10);
  if (*endptr != 0 || t < -1) {
    fprintf(stderr, "Invalid subsong timeout value: %s\n", value);
    return -1;
  }
  uc->subsong_timeout = t;
  uc->subsong_timeout_set = 1;
  return 0;
}


static int uade_set_timeout(struct uade_config *uc, const char *value)
{
  char *endptr;
  int t;
  if (value == NULL) {
    fprintf(stderr, "Must have a parameter value for timeout value in config file %s\n", config_filename);
    return -1;
  }
  t = strtol(value, &endptr, 10);
  if (*endptr != 0 || t < -1) {
    fprintf(stderr, "Invalid timeout value: %s\n", value);
    return -1;
  }
  uc->timeout = t;
  uc->timeout_set = 1;
  return 0;
}
