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
#include <assert.h>
#include <limits.h>
#include <unistd.h>

#include "strlrep.h"
#include "uadeconf.h"
#include "uadeconfig.h"
#include "amigafilter.h"


static int uade_set_silence_timeout(struct uade_config *uc, const char *value);
static int uade_set_subsong_timeout(struct uade_config *uc, const char *value);
static int uade_set_timeout(struct uade_config *uc, const char *value);


static enum uade_option map_str_to_option(const char *key)
{
  size_t i;

  struct optlist {
    char *str;
    int l;
    enum uade_option e;
  };

  struct optlist ol[] = {
    {.str = "action_keys",      .l = 1,  .e = UC_ACTION_KEYS},
    {.str = "buffer_time",      .l = 1,  .e = UC_BUFFER_TIME},
    {.str = "disable_timeout",  .l = 1,  .e = UC_DISABLE_TIMEOUTS},
    {.str = "enable_timeout",   .l = 1,  .e = UC_ENABLE_TIMEOUTS},
    {.str = "filter_type",      .l = 2,  .e = UC_FILTER_TYPE},
    {.str = "force_led_off",    .l = 12, .e = UC_FORCE_LED_OFF},
    {.str = "force_led_on",     .l = 12, .e = UC_FORCE_LED_ON},
    {.str = "force_led",        .l = 9,  .e = UC_FORCE_LED},
    {.str = "gain",             .l = 1,  .e = UC_GAIN},
    {.str = "headphones",       .l = 1,  .e = UC_HEADPHONES},
    {.str = "ignore_player_check", .l = 2, .e = UC_IGNORE_PLAYER_CHECK},
    {.str = "interpolator",     .l = 2,  .e = UC_INTERPOLATOR},
    {.str = "magic_detection",  .l = 1,  .e = UC_MAGIC_DETECTION},
    {.str = "no_filter",        .l = 4,  .e = UC_NO_FILTER},
    {.str = "no_song_end",      .l = 4,  .e = UC_NO_SONG_END},
    {.str = "ntsc",             .l = 2,  .e = UC_NTSC},
    {.str = "one_subsong",      .l = 1,  .e = UC_ONE_SUBSONG},
    {.str = "pal",              .l = 3,  .e = UC_PAL},
    {.str = "panning_value",    .l = 3,  .e = UC_PANNING_VALUE},
    {.str = "random_play",      .l = 2,  .e = UC_RANDOM_PLAY},
    {.str = "recursive_mode",   .l = 2,  .e = UC_RECURSIVE_MODE},
    {.str = "silence_timeout_value", .l = 2, .e = UC_SILENCE_TIMEOUT_VALUE},
    {.str = "song_title",       .l = 2,  .e = UC_SONG_TITLE},
    {.str = "speed_hack",       .l = 2,  .e = UC_SPEED_HACK},
    {.str = "subsong_timeout_value", .l = 2, .e = UC_SUBSONG_TIMEOUT_VALUE},
    {.str = "timeout_value",    .l = 1,  .e = UC_TIMEOUT_VALUE},
    {.str = "verbose",          .l = 1,  .e = UC_VERBOSE},
    {.str = NULL}
  }; 

  for (i = 0; ol[i].str != NULL; i++) {
    if (strncmp(key, ol[i].str, ol[i].l) == 0)
      return ol[i].e;
  }

  return 0;
}


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


/* The function sets the default options. No *_set variables are set because
   we don't want any option to become mergeable by default. See
   uade_merge_configs(). */
void uade_config_set_defaults(struct uade_config *uc)
{
  memset(uc, 0, sizeof(*uc));
  uc->action_keys = 1;
  strlcpy(uc->basedir.name, UADE_CONFIG_BASE_DIR, sizeof uc->basedir.name);
  uade_set_filter_type(uc, NULL);
  uc->gain = 1.0;
  uc->panning = 0.7;
  uc->silence_timeout = 20;
  uc->subsong_timeout = 512;
  uc->timeout = -1;
  uc->use_timeouts = 1;
}


double uade_convert_to_double(const char *value, double def, double low,
			      double high, const char *type)
{
  char *endptr;
  double v;
  if (value == NULL) {
    return def;
  }
  v = strtod(value, &endptr);
  if (*endptr != 0 || v < low || v > high) {
    fprintf(stderr, "Invalid %s value: %s\n", type, value);
    v = def;
  }
  return v;
}


void uade_handle_song_attributes(struct uade_config *uc,
				 char *playername,
				 size_t playernamelen,
				 struct uade_song *us)
{
  struct uade_attribute *a;

  if (us->flags & ES_A500) {
    uc->filter_type_set = 1;
    uc->filter_type = FILTER_MODEL_A500;
  }
  if (us->flags & ES_A1200) {
    uc->filter_type_set = 1;
    uc->filter_type = FILTER_MODEL_A1200;
  }
  if (us->flags & ES_BROKEN_SONG_END)
    uade_set_config_option(uc, UC_NO_SONG_END, NULL);
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
  if (us->flags & ES_NO_FILTER) {
    uc->no_filter_set = 1;
    uc->no_filter = 1;
  }
  if (us->flags & ES_NO_HEADPHONES) {
    uc->headphones_set = 1;
    uc->headphones = 0;
  }
  if (us->flags & ES_NO_PANNING) {
    uc->panning_enable_set = 1;
    uc->panning_enable = 0;
  }
  if (us->flags & ES_NO_POSTPROCESSING) {
    uc->no_postprocessing = 1;
    uc->no_postprocessing_set = 1;
  }
  if (us->flags & ES_NTSC) {
    uc->use_ntsc_set = 1;
    uc->use_ntsc = 1;
  }
  if (us->flags & ES_NTSC)
    uade_set_config_option(uc, UC_NTSC, NULL);
  if (us->flags & ES_ONE_SUBSONG) {
    uc->one_subsong_set = 1;
    uc->one_subsong = 1;
  }
  if (us->flags & ES_PAL)
    uade_set_config_option(uc, UC_PAL, NULL);
  if (us->flags & ES_SPEED_HACK)
    uade_set_config_option(uc, UC_SPEED_HACK, NULL);

  a = us->songattributes;
  while (a != NULL) {
    switch (a->type) {
    case ES_GAIN:
      uc->gain = a->d;
      uc->gain_set = 1;
      uc->gain_enable = 1;
      uc->gain_enable_set = 1;
      break;
    case ES_INTERPOLATOR:
      uade_set_config_option(uc, UC_INTERPOLATOR, a->s);
      break;
    case ES_PANNING:
      uc->panning = a->d;
      uc->panning_set = 1;
      uc->panning_enable = 1;
      uc->panning_enable_set = 1;
      break;
    case ES_PLAYER:
      snprintf(playername, playernamelen, "%s/players/%s", uc->basedir.name, a->s);
      break;
    case ES_SILENCE_TIMEOUT:
      uade_set_config_option(uc, UC_SILENCE_TIMEOUT_VALUE, a->s);
      break;
    case ES_SUBSONGS:
      fprintf(stderr, "Subsongs not implemented.\n");
      break;
    case ES_SUBSONG_TIMEOUT:
      uade_set_config_option(uc, UC_SUBSONG_TIMEOUT_VALUE, a->s);
      break;
    case ES_TIMEOUT:
      uade_set_config_option(uc, UC_TIMEOUT_VALUE, a->s);
      break;
    default:
      fprintf(stderr, "Unknown song attribute integer: 0x%x\n", a->type);
      break;
    }
    a = a->next;
  }

  if (us->flags & ES_VBLANK)
    fprintf(stderr, "vblank song option not implemented.\n");
}


int uade_load_config(struct uade_config *uc, const char *filename)
{
  char line[256];
  FILE *f;
  char *key;
  char *value;
  int linenumber = 0;
  enum uade_option opt;

  if ((f = fopen(filename, "r")) == NULL)
    return 0;

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

    opt = map_str_to_option(key);

    if (opt) {
      uade_set_config_option(uc, opt, value);
    } else {
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
  MERGE_OPTION(basedir);
  MERGE_OPTION(buffer_time);
  MERGE_OPTION(filter_type);
  MERGE_OPTION(led_forced);
  MERGE_OPTION(gain);
  MERGE_OPTION(gain_enable);
  MERGE_OPTION(headphones);
  MERGE_OPTION(ignore_player_check);
  MERGE_OPTION(interpolator);
  MERGE_OPTION(no_filter);
  MERGE_OPTION(no_postprocessing);
  MERGE_OPTION(no_song_end);
  MERGE_OPTION(one_subsong);
  MERGE_OPTION(panning);
  MERGE_OPTION(panning_enable);
  MERGE_OPTION(random_play);
  MERGE_OPTION(recursive_mode);
  MERGE_OPTION(silence_timeout);
  MERGE_OPTION(song_title);
  MERGE_OPTION(speed_hack);
  MERGE_OPTION(subsong_timeout);
  MERGE_OPTION(timeout);
  MERGE_OPTION(use_timeouts);
  MERGE_OPTION(use_ntsc);
  MERGE_OPTION(verbose);
}


int uade_parse_subsongs(int **subsongs, char *option)
{
  char substr[256];
  char *sp, *str;
  size_t pos;
  int nsubsongs;

  nsubsongs = 0;
  *subsongs = NULL;

  if (strlcpy(substr, option, sizeof subsongs) >= sizeof subsongs) {
    fprintf(stderr, "Too long a subsong option: %s\n", option);
    return -1;
  }

  sp = substr;
  while ((str = strsep(&sp, ",")) != NULL) {
    if (*str == 0)
      continue;
    nsubsongs++;
  }

  *subsongs = malloc((nsubsongs + 1) * sizeof((*subsongs)[0]));
  if (*subsongs == NULL) {
    fprintf(stderr, "No memory for subsongs.\n");
    return -1;
  }

  strlcpy(substr, option, sizeof subsongs);

  pos = 0;
  sp = substr;
  while ((str = strsep(&sp, ",")) != NULL) {
    if (*str == 0)
      continue;
    (*subsongs)[pos] = atoi(str);
    pos++;
  }

  (*subsongs)[pos] = -1;
  assert(pos == nsubsongs);

  return nsubsongs;
}


void uade_set_effects(struct uade_effect *effects,
		      const struct uade_config *uc)
{
  if (uc->no_postprocessing)
    uade_effect_disable(effects, UADE_EFFECT_ALLOW);

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


void uade_set_config_option(struct uade_config *uc, enum uade_option opt,
			    const char *value)
{
  char *endptr;
  switch (opt) {
  case UC_ACTION_KEYS:
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
    break;
  case UC_BASE_DIR:
    if (value != NULL) {
      strlcpy(uc->basedir.name, value, sizeof uc->basedir.name);
      uc->basedir_set = 1;
    }
    break;
  case UC_BUFFER_TIME:
    uc->buffer_time_set = 1;
    uc->buffer_time = strtol(value, &endptr, 10);
    if (uc->buffer_time <= 0 || *endptr != 0) {
      fprintf(stderr, "Invalid buffer_time: %s\n", value);
      uc->buffer_time = 0;
    }
    break;
  case UC_DISABLE_TIMEOUTS:
    uc->use_timeouts = 0;
    uc->use_timeouts_set = 1;
    break;
  case UC_ENABLE_TIMEOUTS:
    uc->use_timeouts = 1;
    uc->use_timeouts_set = 1;
    break;
  case UC_FILTER_TYPE:
    uade_set_filter_type(uc, value);
    uc->no_filter_set = 1;
    uc->no_filter = 0;
    break;
  case UC_FORCE_LED:
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 0;
    if (strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) {
    } else if (strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) {
      uc->led_state = 1;
    } else {
      fprintf(stderr, "Unknown force led argument: %s\n", value);
    }
    break;
  case UC_FORCE_LED_OFF:
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 0;
    break;
  case UC_FORCE_LED_ON:
    uc->led_forced_set = 1;
    uc->led_forced = 1;
    uc->led_state = 1;
    break;
  case UC_GAIN:
    uc->gain_enable_set = 1;
    uc->gain_enable = 1;
    uc->gain_set = 1;
    uc->gain = uade_convert_to_double(value, 1.0, 0.0, 128.0, "gain");
    break;
  case UC_HEADPHONES:
    uc->headphones_set = 1;
    uc->headphones = 1;
    break;
  case UC_IGNORE_PLAYER_CHECK:
    uc->ignore_player_check_set = 1;
    uc->ignore_player_check = 1;
    break;
  case UC_INTERPOLATOR:
    if (value == NULL) {
      fprintf(stderr, "uade.conf: No interpolator given.\n");
    } else {
      uc->interpolator_set = 1;
      uc->interpolator = strdup(value);
    }
    break;
  case UC_NO_FILTER:
    uc->no_filter_set = 1;
    uc->no_filter = 1;
    break;
  case UC_NO_SONG_END:
    uc->no_song_end = 1;
    uc->no_song_end_set = 1;
    break;
  case UC_NTSC:
    uc->use_ntsc_set = 1;
    uc->use_ntsc = 1;
    break;
  case UC_ONE_SUBSONG:
    uc->one_subsong_set = 1;
    uc->one_subsong = 1;
    break;
  case UC_PAL:
    uc->use_ntsc_set = 1;
    uc->use_ntsc = 0;
    break;
  case UC_PANNING_VALUE:
    uc->panning_enable_set = 1;
    uc->panning_enable = 1;
    uc->panning_set = 1;
    uc->panning = uade_convert_to_double(value, 0.0, 0.0, 2.0, "panning");
    break;
  case UC_RANDOM_PLAY:
    uc->random_play_set = 1;
    uc->random_play = 1;
    break;
  case UC_RECURSIVE_MODE:
    uc->recursive_mode_set = 1;
    uc->recursive_mode = 1;
    break;
  case UC_SILENCE_TIMEOUT_VALUE:
    uade_set_silence_timeout(uc, value);
    break;
  case UC_SONG_TITLE:
    if (value == NULL) {
      fprintf(stderr, "uade.conf: No song_title format given.\n");
    } else {
      if ((uc->song_title = strdup(value)) == NULL) {
	fprintf(stderr, "No memory for song title format\n");
      } else {
	uc->song_title_set = 1;
      }
    }
    break;
  case UC_SPEED_HACK:
    uc->speed_hack = 1;
    uc->speed_hack_set = 1;
    break;
  case UC_MAGIC_DETECTION:
    uc->magic_detection = 1;
    uc->magic_detection_set = 1;
    break;
  case UC_SUBSONG_TIMEOUT_VALUE:
    uade_set_subsong_timeout(uc, value);
    break;
  case UC_TIMEOUT_VALUE:
    uade_set_timeout(uc, value);
    break;
  case UC_VERBOSE:
    uc->verbose = 1;
    uc->verbose_set = 1;
    break;
  default:
    fprintf(stderr, "Unknown option enum: %d\n", opt);
    exit(-1);
  }
}


void uade_set_ep_attributes(struct uade_config *uc, struct eagleplayer *ep)
{
  if (ep->attributes & ES_A500)
    uade_set_config_option(uc, UC_FILTER_TYPE, "a500");

  if (ep->attributes & ES_A1200)
    uade_set_config_option(uc, UC_FILTER_TYPE, "a1200");

  if (ep->attributes & ES_ALWAYS_ENDS)
    uade_set_config_option(uc, UC_DISABLE_TIMEOUTS, NULL);

  if (ep->attributes & ES_BROKEN_SONG_END)
    uade_set_config_option(uc, UC_NO_SONG_END, NULL);

  if (ep->attributes & ES_CONTENT_DETECTION)
    uade_set_config_option(uc, UC_MAGIC_DETECTION, NULL);

  if (ep->attributes & ES_NTSC)
    uade_set_config_option(uc, UC_NTSC, NULL);

  if (ep->attributes & ES_PAL)
    uade_set_config_option(uc, UC_PAL, NULL);

  if (ep->attributes & ES_SPEED_HACK)
    uade_set_config_option(uc, UC_SPEED_HACK, NULL);
}


void uade_set_filter_type(struct uade_config *uc, const char *model)
{
  uc->filter_type = FILTER_MODEL_A500E;

  if (model == NULL)
    return;

  /* a500 and a500e are the same */
  if (strcasecmp(model, "a500") == 0 || strcasecmp(model, "a500e") == 0) {
    uc->filter_type = FILTER_MODEL_A500E;

    /* a1200 and a1200e are the same */
  } else if (strcasecmp(model, "a1200") == 0 ||
	     strcasecmp(model, "a1200e") == 0) {
    uc->filter_type = FILTER_MODEL_A1200E;

    /* simpler but faster a500/a1200 variants */
  } else if (strcasecmp(model, "a500s") == 0) {
    uc->filter_type = FILTER_MODEL_A500;
  } else if (strcasecmp(model, "a1200s") == 0) {
    uc->filter_type = FILTER_MODEL_A1200;
  } else {
    fprintf(stderr, "Unknown filter model: %s\n", model);
  }
}


static int uade_set_silence_timeout(struct uade_config *uc, const char *value)
{
  char *endptr;
  int t;
  if (value == NULL) {
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


static int uade_set_subsong_timeout(struct uade_config *uc, const char *value)
{
  char *endptr;
  int t;
  if (value == NULL) {
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
