#ifndef _UADE_EAGLEPLAYER_H_
#define _UADE_EAGLEPLAYER_H_

#include <stdio.h>
#include <stdint.h>

#define EP_A500              (1 << 0)
#define EP_A1200             (1 << 1)
#define EP_ALWAYS_ENDS       (1 << 2)
#define EP_CONTENT_DETECTION (1 << 3)
#define EP_SPEED_HACK        (1 << 4)

#define ES_BROKEN_SUBSONGS   (1 << 0)
#define ES_LED_OFF           (1 << 1)
#define ES_LED_ON            (1 << 2)
#define ES_NO_HEADPHONES     (1 << 3)
#define ES_NO_PANNING        (1 << 4)
#define ES_NO_POSTPROCESSING (1 << 5)
#define ES_NTSC              (1 << 6)
#define ES_SPEEDHACK         (1 << 7)
#define ES_VBLANK            (1 << 8)

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

struct eaglesong {
  int flags;
  int nsubsongs;
  uint8_t *subsongs;
  char md5[33];
};

struct eagleplayer *uade_analyze_file_format(const char *modulename,
					     const char *basedir, int verbose);

struct eagleplayer *uade_get_eagleplayer(const char *extension, 
					 struct eagleplayerstore *playerstore);

struct eagleplayerstore *uade_read_eagleplayer_conf(const char *filename);

#endif
