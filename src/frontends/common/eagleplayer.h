#ifndef _UADE_EAGLEPLAYER_H_
#define _UADE_EAGLEPLAYER_H_

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

#include <uadeconfstructure.h>


#define ES_A500              (1 <<  0)
#define ES_A1200             (1 <<  1)
#define ES_ALWAYS_ENDS       (1 <<  2)
#define ES_BROKEN_SONG_END   (1 <<  3)
#define ES_CONTENT_DETECTION (1 <<  4)
#define ES_GAIN              (1 <<  5)
#define ES_RESAMPLER         (1 <<  6)
#define ES_LED_OFF           (1 <<  7)
#define ES_LED_ON            (1 <<  8)
#define ES_NEVER_ENDS        (1 <<  9)
#define ES_NO_FILTER         (1 << 10)
#define ES_NO_HEADPHONES     (1 << 11)
#define ES_NO_PANNING        (1 << 12)
#define ES_NO_POSTPROCESSING (1 << 13)
#define ES_NTSC              (1 << 14)
#define ES_ONE_SUBSONG       (1 << 15)
#define ES_PAL               (1 << 16)
#define ES_PANNING           (1 << 17)
#define ES_PLAYER            (1 << 18)
#define ES_REJECT            (1 << 19)
#define ES_SILENCE_TIMEOUT   (1 << 20)
#define ES_SPEED_HACK        (1 << 21)
#define ES_SUBSONGS          (1 << 22)
#define ES_SUBSONG_TIMEOUT   (1 << 23)
#define ES_TIMEOUT           (1 << 24)
#define ES_VBLANK            (1 << 25)


struct eagleplayer {
  char *playername;
  size_t nextensions;
  char **extensions;
  int flags;
  struct uade_attribute *attributelist;
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


enum uade_attribute_type {
  UA_STRING = 1,
  UA_INT,
  UA_DOUBLE
};


struct uade_attribute;

struct uade_attribute {
  struct uade_attribute *next;
  enum uade_attribute_type type;
  char *s;
  int i;
  double d;
};

struct uade_song {
  int flags;
  int nsubsongs;
  uint8_t *subsongs;
  struct uade_attribute *songattributes;

  char md5[33];

  char module_filename[PATH_MAX];

  char playername[256]; /* Eagleplayer name in players directory */
  char modulename[256]; /* From score */
  char formatname[256];

  int playtime;

  uint8_t *buf;
  size_t bufsize;

  int min_subsong;
  int max_subsong;
  int cur_subsong;

  int64_t out_bytes;
};


int uade_add_playtime(const char *md5, uint32_t playtime, int replaceandsort);
struct uade_song *uade_alloc_song(const char *filename);
struct eagleplayer *uade_analyze_file_format(const char *modulename,
					     struct uade_config *uc);
struct eagleplayer *uade_get_eagleplayer(const char *extension, 
					 struct eagleplayerstore *playerstore);
int uade_read_content_db(const char *filename);
struct eagleplayerstore *uade_read_eagleplayer_conf(const char *filename);
int uade_read_song_conf(const char *filename);
void uade_save_content_db(const char *filename);
void uade_unalloc_song(struct uade_song *us);
int uade_update_song_conf(const char *songconfin, const char *songconfout,
			  const char *songname, const char *options);
#endif
