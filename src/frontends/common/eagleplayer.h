#ifndef _UADE_EAGLEPLAYER_H_
#define _UADE_EAGLEPLAYER_H_

#include <stdio.h>

#define EP_A500              (1 << 0)
#define EP_A1200             (1 << 1)
#define EP_ALWAYS_ENDS       (1 << 2)
#define EP_CONTENT_DETECTION (1 << 3)
#define EP_SPEED_HACK        (1 << 4)

struct eagleplayer {
  char *playername;
  size_t nextensions;
  char **extensions;
  int attributes;
};

struct eagleplayermap {
  char *extension;
  struct eagleplayer *player;
};

struct eagleplayerstore {
  size_t nplayers;
  struct eagleplayer *players;
  size_t nextensions;
  struct eagleplayermap *map;
};

struct eagleplayer *uade_analyze_file_format(const char *modulename,
					     const char *basedir, int verbose);

struct eagleplayer *uade_get_eagleplayer(const char *extension, 
					 struct eagleplayerstore *playerstore);

struct eagleplayerstore *uade_read_eagleplayer_conf(const char *filename);

#endif
