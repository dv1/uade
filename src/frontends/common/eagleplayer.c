/* UADE
 *
 * Copyright 2005 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * Loads contents of 'eagleplayer.conf' and 'song.conf'. The file formats are
 * specified in doc/eagleplayer.conf and doc/song.conf.
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) in any you
 * want in your projects.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>

#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <strlrep.h>

#include <eagleplayer.h>
#include <amifilemagic.h>


#define LINESIZE (1024)
#define WS_DELIMITERS " \t\n"
#define OPTION_DELIMITER ","

#define eperror(fmt, args...) do { fprintf(stderr, "Eagleplayer.conf error on line %zd: " fmt "\n", lineno, ## args); exit(-1); } while (0)


static uint8_t fileformat_buf[8192];
static struct eagleplayerstore *playerstore;


static int ufcompare(const void *a, const void *b);


struct eagleplayer *uade_analyze_file_format(const char *modulename,
					     const char *basedir, int verbose)
{
  struct stat st;
  char extension[11];
  FILE *f;
  size_t readed;
  struct eagleplayer *candidate;
  char *t, *tn;
  int len;
  static int warnings = 1;

  if ((f = fopen(modulename, "r")) == NULL) {
    fprintf(stderr, "Can not open module: %s\n", modulename);
    return NULL;
  }
  if (fstat(fileno(f), &st)) {
    fprintf(stderr, "Very weird stat error: %s (%s)\n", modulename, strerror(errno));
    exit(-1);
  }
  readed = fread(fileformat_buf, 1, sizeof(fileformat_buf), f);
  fclose(f);
  if (readed == 0)
    return NULL;
  memset(&fileformat_buf[readed], 0, sizeof(fileformat_buf) - readed);
  uade_filemagic(fileformat_buf, extension, st.st_size, sizeof(fileformat_buf));

  if (verbose)
    fprintf(stderr, "%s: deduced extension: %s\n", modulename, extension);

  if (playerstore == NULL) {
    char formatsfile[PATH_MAX];
    snprintf(formatsfile, sizeof(formatsfile), "%s/eagleplayer.conf", basedir);
    if ((playerstore = uade_read_eagleplayer_conf(formatsfile)) == NULL) {
      if (warnings)
	fprintf(stderr, "Tried to load uadeformats file from %s, but failed\n", formatsfile);
      warnings = 0;
      return NULL;
    }
  }

  /* if filemagic found a match, we'll use player plugins associated with
     that extension. if filemagic didn't find a match, we'll try to parse
     pre- and postfixes from the modulename */

  if (extension[0]) {
    candidate = uade_get_eagleplayer(extension, playerstore);
    if (candidate)
      return candidate;
    fprintf(stderr, "Deduced file extension (%s) is not on the uadeformats list.\n", extension);
  }

  /* magic wasn't able to deduce the format, so we'll try prefix and postfix
     from modulename */
  t = strrchr(modulename, (int) '/');
  if (t == NULL) {
    t = (char *) modulename;
  } else {
    t++;
  }

  /* try prefix first */
  tn = strchr(t, '.');
  if (tn == NULL) {
    fprintf(stderr, "Unknown format: %s\n", modulename);
    return NULL;
  }
  len = ((intptr_t) tn) - ((intptr_t) t);
  if (len < sizeof(extension)) {
    memcpy(extension, t, len);
    extension[len] = 0;
    candidate = uade_get_eagleplayer(extension, playerstore);
    if (candidate && (candidate->attributes & EP_CONTENT_DETECTION) == 0)
      return candidate;
  }

  /* prefix didn't match anything. trying postfix */
  t = strrchr(t, '.');
  if (strlcpy(extension, t + 1, sizeof(extension)) >= sizeof(extension)) {
    /* too long to be an extension */
    fprintf(stderr, "Unknown format: %s\n", modulename);
    return NULL;
  }

  candidate = uade_get_eagleplayer(extension, playerstore);
  if (candidate && (candidate->attributes & EP_CONTENT_DETECTION) == 0)
    return candidate;

  return NULL;
}


struct eagleplayer *uade_get_eagleplayer(const char *extension, struct eagleplayerstore *ps)
{
  struct eagleplayermap *uf = ps->map;
  struct eagleplayermap *f;
  struct eagleplayermap key = {.extension = (char *) extension};

  f = bsearch(&key, uf, ps->nextensions, sizeof(uf[0]), ufcompare);
  if (f == NULL)
    return NULL;

  return f->player;
}


/* Split line with respect to white space. */
static char **split_line(size_t *nitems, size_t *lineno, FILE *f,
			 const char *delimiters)
{
  char line[LINESIZE], templine[LINESIZE];
  char **items = NULL;
  size_t pos;
  char *sp, *s;

  *nitems = 0;

  while (fgets(line, sizeof line, f) != NULL) {

    if (lineno != NULL)
      (*lineno)++;

    /* Skip, if a comment line */
    if (line[0] == '#')
      continue;

    /* strsep() modifies line that it touches, so we make a copy of it, and
       then count the number of items on the line */
    strlcpy(templine, line, sizeof(templine));
    sp = templine;
    while ((s = strsep(&sp, delimiters)) != NULL) {
      if (*s == 0)
	continue;
      (*nitems)++;
    }

    if (*nitems > 0)
      break;
  }

  if (*nitems == 0)
    return NULL;

  if ((items = malloc(sizeof(items[0]) * (*nitems + 1))) == NULL) {
    fprintf(stderr, "No memory for nws items.\n");
    exit(-1);
  }

  sp = line;
  pos = 0;
  while ((s = strsep(&sp, delimiters)) != NULL) {
    if (*s == 0)
      continue;
    if ((items[pos] = strdup(s)) == NULL) {
      fprintf(stderr, "No memory for an nws item.\n");
      exit(-1);
    }
    pos++;
  }
  items[pos] = NULL;
  assert(pos == *nitems);

  return items;
}


/* Read eagleplayer.conf. */
struct eagleplayerstore *uade_read_eagleplayer_conf(const char *filename)
{
  FILE *f;
  struct eagleplayer *p;
  size_t allocated;
  size_t lineno = 0;
  struct eagleplayerstore *ps = NULL;
  size_t exti;
  size_t i;

  f = fopen(filename, "r");
  if (f == NULL)
    goto error;

  ps = calloc(1, sizeof ps[0]);
  if (ps == NULL)
    eperror("No memory for ps.");

  allocated = 16;
  if ((ps->players = malloc(allocated * sizeof(ps->players[0]))) == NULL)
    eperror("No memory for eagleplayer.conf file.\n");

  while (1) {

    char **items;
    size_t nitems;

    if ((items = split_line(&nitems, &lineno, f, WS_DELIMITERS)) == NULL)
      break;

    assert(nitems > 0);

    if (ps->nplayers == allocated) {
      allocated *= 2;
      ps->players = realloc(ps->players, allocated * sizeof(ps->players[0]));
      if (ps->players == NULL)
	eperror("No memory for players.");
    }

    p = &ps->players[ps->nplayers];
    ps->nplayers++;

    memset(p, 0, sizeof p[0]);

    p->playername = strdup(items[0]);
    if (p->playername == NULL) {
      fprintf(stderr, "No memory for playername.\n");
      exit(-1);
    }

    for (i = 1; i < nitems; i++) {
      if (strncasecmp(items[i], "prefixes=", 9) == 0) {
	char prefixes[LINESIZE];
	char *prefixstart = items[i] + 9;
	char *sp, *s;
	size_t pos;

	assert(p->nextensions == 0 && p->extensions == NULL);
	
	p->nextensions = 0;
	strlcpy(prefixes, prefixstart, sizeof(prefixes));
	sp = prefixes;
	while ((s = strsep(&sp, OPTION_DELIMITER)) != NULL) {
	  if (*s == 0)
	    continue;
	  p->nextensions++;
	}

	p->extensions = malloc(p->nextensions * (1 + sizeof(p->extensions[0])));
	if (p->extensions == NULL)
	  eperror("No memory for extensions.");

	pos = 0;
	sp = prefixstart;
	while ((s = strsep(&sp, OPTION_DELIMITER)) != NULL) {
	  if (*s == 0)
	    continue;
	  if ((p->extensions[pos] = strdup(s)) == NULL)
	    eperror("No memory for prefix.");
	  pos++;
	}
	p->extensions[pos] = NULL;
	assert(pos == p->nextensions);

      } else if (strcasecmp(items[i], "a500") == 0) {
	p->attributes |= EP_A500;
      } else if (strcasecmp(items[i], "a1200") == 0) {
	p->attributes |= EP_A1200;
      } else if (strcasecmp(items[i], "always_ends") == 0) {
	p->attributes |= EP_ALWAYS_ENDS;
      } else if (strcasecmp(items[i], "content_detection") == 0) {
	p->attributes |= EP_CONTENT_DETECTION;
      } else if (strcasecmp(items[i], "speed_hack") == 0) {
	p->attributes |= EP_SPEED_HACK;
      } else if (strncasecmp(items[i], "comment:", 8) == 0) {
	break;
      } else {
	fprintf(stderr, "Unrecognized option: %s\n", items[i]);
      }
    }

    free(items);
  }

  fclose(f);

  if (ps->nplayers == 0) {
    free(ps->players);
    free(ps);
    return NULL;
  }

  for (i = 0; i < ps->nplayers; i++)
    ps->nextensions += ps->players[i].nextensions;

  ps->map = malloc(sizeof(ps->map[0]) * ps->nextensions);
  if (ps->map == NULL)
    eperror("No memory for extension map.");

  exti = 0;
  for (i = 0; i < ps->nplayers; i++) {
    size_t j;
    if (exti >= ps->nextensions) {
      fprintf(stderr, "pname %s\n", ps->players[i].playername);
      fflush(stderr);
    }
    assert(exti < ps->nextensions);
    p = &ps->players[i];
    for (j = 0; j < p->nextensions; j++) {
      ps->map[exti].player = p;
      ps->map[exti].extension = p->extensions[j];
      exti++;
    }
  }

  assert(exti == ps->nextensions);

  qsort(ps->map, ps->nextensions, sizeof(ps->map[0]), ufcompare);

  return ps;

 error:
  free(ps->players);
  free(ps);
  if (f != NULL)
    fclose(f);
  return NULL;
}


/* Compare function for bsearch() and qsort() */
static int ufcompare(const void *a, const void *b)
{
  const struct eagleplayermap *ua = a;
  const struct eagleplayermap *ub = b;
  return strcasecmp(ua->extension, ub->extension);
}
