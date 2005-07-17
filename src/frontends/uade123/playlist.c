/* Copyright (C) Heikki Orsila 2003-2005
   email: heikki.orsila@iki.fi
   License: LGPL and GPL
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include <unixwalkdir.h>

#include "playlist.h"
#include "uade123.h"

int playlist_init(struct playlist *pl)
{
  int ret;
  pl->lower_bound = 0;
  pl->upper_bound = 0;
  pl->randomize = 0;
  pl->repeat = 0;
  ret = chrarray_init(&pl->list);
  pl->valid = ret ? 1 : 0;
  return ret;
}


/* enable == 0: disable random play
   enable == 1: enable random play
   enable == -1: toggle random play state between enabled and disabled */
int playlist_random(struct playlist *pl, int enable)
{
  if (enable < 0) {
    pl->randomize = pl->randomize ? 0 : 1;
  } else {
    pl->randomize = enable ? 1 : 0;
  }
  srandom(time(0));
  return pl->randomize;
}


void playlist_repeat(struct playlist *pl)
{
  pl->repeat = 1;
}


int playlist_empty(struct playlist *pl)
{
  if (!pl->valid) {
    fprintf(stderr, "playlist invalid\n");
    return 1;
  }
  if (!pl->list.n_entries)
    return 1;
  if (pl->repeat)
    return 0;
  if (pl->randomize) {
    return pl->upper_bound == 0;
  } else {
    return pl->lower_bound == pl->list.n_entries;
  }
}


static void *recursive_func(const char *file, enum uade_wtype wtype, void *pl)
{
  if (wtype == UADE_WALK_REGULAR_FILE) {
    if (!playlist_add(pl, file, 0))
      fprintf(stderr, "error enqueuing %s\n", file);
  }
  return NULL;
}


int playlist_add(struct playlist *pl, const char *name, int recursive)
{
  int ret;
  struct stat st;

  if (!pl->valid)
    return 0;

  if (stat(name, &st))
    return 0;

  if (S_ISREG(st.st_mode)) {
    /* fprintf(stderr, "enqueuing regular: %s\n", name); */
    ret = chrarray_add(&pl->list, name, strlen(name) + 1);
  } else if (S_ISDIR(st.st_mode)) {
    /* add directories to playlist only if 'recursive' is non-zero */
    if (recursive) {
      uade_walk_directories(name, recursive_func, pl);
    } else {
      debug("Not adding directory %s. Use -r to add recursively.\n", name);
    }
    ret = 1;
  }

  pl->upper_bound = pl->list.n_entries;
  return ret;
}


int playlist_get_next(char *name, size_t maxlen, struct playlist *pl)
{
  int len;
  char *s;
  if (!pl->valid)
    return 0;
  if (!pl->list.n_entries)
    return 0;
  if (!maxlen) {
    fprintf(stderr, "uade123: playlist_get_next(): given maxlen = 0\n");
    return 0;
  }

  if (pl->randomize) {
    int i;
    struct chrentry t;
    /* take a random entry from chrarray. basically this suffles an entry
       from a random position to the tail of the array */
    if (!pl->upper_bound) {
      if (!pl->repeat)
	return 0;
      pl->upper_bound = pl->list.n_entries;
    }
    i = random() % pl->upper_bound; /* improper use of random() */
    t = pl->list.entries[i];
    if (i != (pl->upper_bound - 1)) {
      /* not end of the list => need to shuffle (swap places) */
      pl->list.entries[i] = pl->list.entries[pl->upper_bound - 1];
      pl->list.entries[pl->upper_bound - 1] = t;
    }
    s = &pl->list.data[t.off];
    len = t.len;
    /* decrease upper_bound. the entry chosen this time is at the tail, so
       it won't be chosen next time (unless if upper_bound is increased
       due to repeat) */
    pl->upper_bound--;

  } else {

    /* non-random pick. take the first from the list, and increase
       lower_bound (unless we are repeating this time) */
    if (pl->lower_bound == pl->list.n_entries) {
      if (!pl->repeat) {
	return 0;
      }
      pl->lower_bound = 0;
    }
    s = &pl->list.data[pl->list.entries[pl->lower_bound].off];
    len = pl->list.entries[pl->lower_bound].len;
    pl->lower_bound++;
  }
  if (len > maxlen) {
    fprintf(stderr, "uade: playlist_get_next(): too long a string: %s\n", s);
    return 0;
  }
  memcpy(name, s, len);
  return 1;
}


void playlist_flush(struct playlist *pl)
{
  chrarray_flush(&pl->list);
}
