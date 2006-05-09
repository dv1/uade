#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <strlrep.h>

#include "players.h"

/* This is called when an eagleplayer queries for attributes. The query result
   is returned through 'dst', and the result is at most maxlen bytes long.
   'src' contains the full query. */
int uade_get_info(char *dst, char *src, int maxlen)
{
  int ret = -1;
  /* Return the amount of bytes that a full result needs, or -1. */
  if (strcasecmp(src, "VBLANK") == 0) {
    ret = strlcpy(dst, "No", maxlen) + 1;
  } else if (strcasecmp(src, "eagleoptions") == 0) {
    memcpy(dst, "foo\0bar\0", 8);
    ret = 8;
  }
  return ret;
}
