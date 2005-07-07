#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "players.h"

#include "../osdep/strl.c"

/* attribute variables */

static int vblank_mode = 0; /* VBLANK attribute */


/* file magic for various tracker modules */

static char *mod_face_another_day[] = {
  "0x00", "face another day",
  "0x14", "by jogeir liljedahl",
  "0x32", "(c) 1991 noiseless",
  0
};

static char **mod_list[] = {mod_face_another_day, 0};


/* this is called for any tracker module that is played with PTK-Prowiz.
   it tries to detect if vblank timing should be used. currently it uses
   blacklisting method. it is not possibly to create a generic method. */

static void mod_blacklist_check(unsigned char *song, int len)
{
  char **file_magic;
  char *endptr;
  int i, j;
  int offset;
  int slen;

  i = 0;
  while ((file_magic = mod_list[i])) {
    j = 0;
    while (1) {
      if (file_magic[j] == 0) {
	fprintf(stderr, "uade: the player should use vblank attribute\n");
	vblank_mode = 1;
	return;
      }
      offset = strtol(file_magic[j], &endptr, 0);
      if (*endptr) {
	fprintf(stderr, "magic error: %s %s\n", file_magic[j], file_magic[j + 1]);
	break;
      }
      if (offset < 0 || offset >= len) {
	fprintf(stderr, "magic offset error: %s %s\n", file_magic[j], file_magic[j + 1]);
	break;
      }
      slen = strlen(file_magic[j + 1]) + 1;
      /* paranoid as hell. */
      if (slen <= 0) {
	fprintf(stderr, "magic offset error: %d <= 0\n", slen);
	break;
      }
      /* not so paranoid */
      if ((offset + slen) >= len) {
	fprintf(stderr, "magic goes over the file end.\n");
	break;
      }
      if (strcmp(file_magic[j + 1], song + offset)) {
	/* fprintf(stderr, "didn't satisfy vblank attribute.\n"); */
	break;
      }
      j += 2;
    }
    i++;
  }
}


/* this is called once for a song, just after it has been loaded into memory
   before playing. it checks whether the song/player matches some
   attribute of interest. one attribute is VBLANK which is relevant
   for PTK-Prowiz. The Code in this modules tries to detect if vblank
   timing is needed. The PTK-Prowiz player asks this module through
   uade_get_info() if it should be used or not. */

void uade_player_attribute_check(char *songname, char *playername,
				 unsigned char *song, int len)
{
  char *p = strrchr(playername, (int) '/');
  p = (p == NULL) ? playername : p + 1;

  /* default initializations */
  vblank_mode = 0;

  if (strcmp(p, "PTK-Prowiz") == 0) {
    /* fprintf(stderr, "it's PTK. Doing checks.\n"); */
    mod_blacklist_check(song, len);
  }
}



/* this is called when an eagler player queries for attributes.
   it fills char *dst with proper data if the corresponding attribute 
   exists and is active. */

int uade_get_info(char *dst, char *src, int maxlen)
{
  /* fprintf(stderr, "uade: score requesting info on %s\n", src); */
  if (strcmp(src, "VBLANK") == 0) {
    int ret = strlcpy(dst, vblank_mode ? "Yes" : "No", maxlen);
    if (vblank_mode)
      fprintf(stderr, "uade: using vblank\n");
    return ret + 1; /* the space the response would have consumed */
  }
  return -1;
}
