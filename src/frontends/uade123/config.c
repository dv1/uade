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

#include "config.h"
#include "uade123.h"


static char *config_filename = NULL;


void config_set_panning(const char *value)
{
  char *endptr;
  if (value == NULL) {
    fprintf(stderr, "must have a parameter value for panning value in config file %s\n", config_filename);
    exit(-1);
  }
  uade_panning_value = strtod(value, &endptr);
  if (*endptr != 0 || uade_panning_value < 0.0 || uade_panning_value > 2.0) {
    fprintf(stderr, "uade123: illegal panning value: %f\n", uade_panning_value);
    exit(-1);
  }
  uade_use_panning = 1;
}


void config_set_silence_timeout(const char *value)
{
  char *endptr;
  if (value == NULL || value[0] == 0) {
    fprintf(stderr, "must have a parameter value for silence timeout in config file %s\n", config_filename);
    exit(-1);
  }
  uade_silence_timeout = strtol(value, &endptr, 10);
  if (*endptr != 0 || uade_silence_timeout < -1) {
    fprintf(stderr, "uade123: illegal silence timeout value: %s\n", value);
    exit(-1);
  }
}


void config_set_subsong_timeout(const char *value)
{
  char *endptr;
  if (value == NULL || value[0] == 0) {
    fprintf(stderr, "must have a parameter value for subsong timeout in config file %s\n", config_filename);
    exit(-1);
  }
  uade_subsong_timeout = strtol(value, &endptr, 10);
  if (*endptr != 0 || uade_subsong_timeout < -1) {
    fprintf(stderr, "uade123: illegal subsong timeout value: %s\n", value);
    exit(-1);
  }
}


void config_set_timeout(const char *value)
{
  char *endptr;
  if (value == NULL) {
    fprintf(stderr, "must have a parameter value for timeout value in config file %s\n", config_filename);
    exit(-1);
  }
  uade_timeout = strtol(value, &endptr, 10);
  if (*endptr != 0 || uade_timeout < -1) {
    fprintf(stderr, "uade123: illegal timeout value: %s\n", value);
    exit(-1);
  }
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


int load_config(const char *filename)
{
  char line[256];
  FILE *f;
  char *key;
  char *value;
  int linenumber = 0;
  if ((f = fopen(filename, "r")) == NULL)
    return 0;

  config_filename = (char *) filename;

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
    if (strncmp(key, "action_keys", 6) == 0) {
      uade_terminal_mode = 1;
    } else if (strncmp(key, "ignore_player_check", 6) == 0) {
      uade_ignore_player_check = 1;
    } else if (strncmp(key, "one_subsong", 3) == 0) {
      uade_one_subsong_per_file = 1;
    } else if (strncmp(key, "panning_value", 3) == 0) {
      config_set_panning(value);
    } else if (strncmp(key, "random_play", 6) == 0) {
      playlist_random(&uade_playlist, 1);
    } else if (strncmp(key, "recursive_mode", 9) == 0) {
      uade_recursivemode = 1;
    } else if (strncmp(key, "silence_timeout_value", 7) == 0) {
      config_set_silence_timeout(value);
    } else if (strncmp(key, "subsong_timeout_value", 7) == 0) {
      config_set_subsong_timeout(value);
    } else if (strncmp(key, "timeout_value", 7) == 0) {
      config_set_timeout(value);
    } else {
      fprintf(stderr, "unknown config key in %s on line %d: %s\n", filename, linenumber, key);
    }
  }

  fclose(f);
  config_filename = NULL;
  return 1;
}
