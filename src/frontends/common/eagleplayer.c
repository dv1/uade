/* UADE
 *
 * Copyright 2005 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * Loads contents of 'eagleplayer.conf' and 'song.conf'. The file formats are
 * specified in doc/uade123.1.
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
#include <fcntl.h>
#include <unistd.h>

#include <strlrep.h>

#include "eagleplayer.h"
#include "amifilemagic.h"
#include "md5.h"
#include "uadeconf.h"
#include "unixatomic.h"


#define LINESIZE (1024)
#define WS_DELIMITERS " \t\n"
#define OPTION_DELIMITER ","

#define eperror(fmt, args...) do { fprintf(stderr, "Eagleplayer.conf error on line %zd: " fmt "\n", lineno, ## args); exit(-1); } while (0)


struct attrlist {
  char *s;
  int e;
  enum uade_attribute_type t;
};


struct contentchecksum {
  uint32_t playtime; /* in milliseconds */
  char md5[33];
};


struct eaglesong {
  int flags;
  char md5[33];
  struct uade_attribute *attributes;
};


static struct contentchecksum *contentchecksums;
static size_t nccalloc;
static size_t nccused;
static int ccmodified;

static struct eagleplayerstore *playerstore;

static int nsongs;
static struct eaglesong *songstore;


static int ufcompare(const void *a, const void *b);
static void md5_from_buffer(char *dest, size_t destlen,
			    uint8_t *buf, size_t bufsize);
static void uade_analyze_song(struct uade_song *us);
static int uade_find_playtime(const char *md5);


/* Compare function for bsearch() and qsort() to sort songs with respect
   to their md5sums */
static int escompare(const void *a, const void *b)
{
  return strcasecmp(((struct eaglesong *) a)->md5,
		    ((struct eaglesong *) b)->md5);
}


static int contentcompare(const void *a, const void *b)
{
  return strcasecmp(((struct contentchecksum *) a)->md5,
		    ((struct contentchecksum *) b)->md5);
}


/* replace must be zero if content db is unsorted */
int uade_add_playtime(const char *md5, uint32_t playtime, int replaceandsort)
{
  if (contentchecksums == NULL)
    return 0;
  /* Do not record song shorter than 5 secs */
  if (playtime < 5000)
    return 1;
  if (strlen(md5) != 32)
    return 0;

  if (replaceandsort) {
    struct contentchecksum key;
    struct contentchecksum *n;
    strlcpy(key.md5, md5, sizeof key.md5);
    n = bsearch(&key, contentchecksums, nccused, sizeof contentchecksums[0], contentcompare);
    if (n != NULL) {
      strlcpy(n->md5, md5, sizeof(n->md5));
      if (n->playtime != playtime)
	ccmodified = 1;
      n->playtime = playtime;
      return 1;
    }
  }
  if (nccused == nccalloc) {
    struct contentchecksum *n;
    nccalloc *= 2;
    n = realloc(contentchecksums, nccalloc * sizeof(struct contentchecksum));
    if (n == NULL) {
      fprintf(stderr, "uade: No memory for new md5s.\n");
      return 0;
    }
    contentchecksums = n;
  }
  strlcpy(contentchecksums[nccused].md5, md5, sizeof(contentchecksums[nccused].md5));
  contentchecksums[nccused].playtime = playtime;
  nccused++;
  ccmodified = 1;
  if (replaceandsort)
    qsort(contentchecksums, nccused, sizeof contentchecksums[0], contentcompare);
  return 1;
}


struct uade_song *uade_alloc_song(const char *filename)
{
  struct uade_song *us = NULL;

  if ((us = calloc(1, sizeof *us)) == NULL)
    goto error;

  us->min_subsong = us->max_subsong = us->cur_subsong = -1;
  us->playtime = -1;

  strlcpy(us->module_filename, filename, sizeof us->module_filename);

  us->buf = atomic_read_file(&us->bufsize, filename);
  if (us->buf == NULL)
    goto error;

  /* Get song specific flags and info based on the md5sum */
  uade_analyze_song(us);
  return us;

 error:
  if (us != NULL) {
    free(us->buf);
    free(us);
  }
  return NULL;
}


static struct eagleplayer *analyze_file_format(int *content,
					       const char *modulename,
					       const char *basedir,
					       int verbose)
{
  struct stat st;
  char extension[11];

  FILE *f;
  size_t readed;
  struct eagleplayer *candidate;
  char *t, *tn;
  int len;
  static int warnings = 1;
  size_t bufsize;
  uint8_t fileformat_buf[8192];

  *content = 0;

  if ((f = fopen(modulename, "rb")) == NULL) {
    fprintf(stderr, "Can not open module: %s\n", modulename);
    return NULL;
  }
  if (fstat(fileno(f), &st)) {
    fprintf(stderr, "Very weird stat error: %s (%s)\n", modulename, strerror(errno));
    exit(-1);
  }
  bufsize = sizeof fileformat_buf;
  readed = atomic_fread(fileformat_buf, 1, bufsize, f);
  fclose(f);
  if (readed == 0)
    return NULL;
  memset(&fileformat_buf[readed], 0, bufsize - readed);
  bufsize = readed;

  uade_filemagic(fileformat_buf, bufsize, extension, st.st_size, verbose);

  if (verbose)
    fprintf(stderr, "%s: deduced extension: %s\n", modulename, extension);

  if (strcmp(extension, "packed") == 0)
    return NULL;

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

    if (candidate) {
      *content = 1;
      return candidate;
    }

    if (verbose)
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
  if (tn == NULL)
    return NULL;

  len = ((intptr_t) tn) - ((intptr_t) t);
  if (len < sizeof(extension)) {
    memcpy(extension, t, len);
    extension[len] = 0;

    if ((candidate = uade_get_eagleplayer(extension, playerstore)) != NULL)
      return candidate;
  }

  /* prefix didn't match anything. trying postfix */
  t = strrchr(t, '.');
  if (strlcpy(extension, t + 1, sizeof(extension)) >= sizeof(extension)) {
    /* too long to be an extension */
    return NULL;
  }

  if ((candidate = uade_get_eagleplayer(extension, playerstore)) != NULL)
    return candidate;

  return NULL;
}


static int parse_attribute(struct uade_attribute **attributelist, int *flags,
			   char *item, size_t lineno)
{
  size_t i;
  size_t len;

  struct attrlist esattrs[] = {
    {.s = "a500",            .e = ES_A500},
    {.s = "a1200",           .e = ES_A1200},
    {.s = "always_ends",     .e = ES_ALWAYS_ENDS},
    {.s = "broken_song_end", .e = ES_BROKEN_SONG_END},
    {.s = "content_detection", .e = ES_CONTENT_DETECTION},
    {.s = "led_off",         .e = ES_LED_OFF},
    {.s = "led_on",          .e = ES_LED_ON},
    {.s = "never_ends",      .e = ES_NEVER_ENDS},
    {.s = "no_filter",       .e = ES_NO_FILTER},
    {.s = "no_headphones",   .e = ES_NO_HEADPHONES},
    {.s = "no_panning",      .e = ES_NO_PANNING},
    {.s = "no_postprocessing", .e = ES_NO_POSTPROCESSING},
    {.s = "ntsc",            .e = ES_NTSC},
    {.s = "one_subsong",     .e = ES_ONE_SUBSONG},
    {.s = "pal",             .e = ES_PAL},
    {.s = "reject",          .e = ES_REJECT},
    {.s = "speed_hack",      .e = ES_SPEED_HACK},
    {.s = NULL}
  };

  struct attrlist esvalueattrs[] = {
    {.s = "epopt",           .t = UA_STRING, .e = ES_EP_OPTION},
    {.s = "gain",            .t = UA_STRING, .e = ES_GAIN},
    {.s = "interpolator",    .t = UA_STRING, .e = ES_RESAMPLER},
    {.s = "panning",         .t = UA_STRING, .e = ES_PANNING},
    {.s = "player",          .t = UA_STRING, .e = ES_PLAYER},
    {.s = "resampler",       .t = UA_STRING, .e = ES_RESAMPLER},
    {.s = "silence_timeout", .t = UA_STRING, .e = ES_SILENCE_TIMEOUT},
    {.s = "subsong_timeout", .t = UA_STRING, .e = ES_SUBSONG_TIMEOUT},
    {.s = "subsongs",        .t = UA_STRING, .e = ES_SUBSONGS},
    {.s = "timeout",         .t = UA_STRING, .e = ES_TIMEOUT},
    {.s = NULL}
  };

  for (i = 0; esattrs[i].s != NULL; i++) {
    if (strcasecmp(item, esattrs[i].s) == 0) {
      *flags |= esattrs[i].e;
      return 1;
    }
  }

  for (i = 0; esvalueattrs[i].s != NULL; i++) {
    len = strlen(esvalueattrs[i].s);
    if (strncasecmp(item, esvalueattrs[i].s, len) == 0) {
      struct uade_attribute *a;
      char *str;
      char *endptr;
      int success;

      if (item[len] != '=') {
	fprintf(stderr, "Invalid song item: %s\n", item);
	break;
      }
      str = item + len + 1;
      
      if ((a = calloc(1, sizeof *a)) == NULL)
	eperror("No memory for song attribute.\n");
      
      success = 0;
      
      switch (esvalueattrs[i].t) {
      case UA_DOUBLE:
	a->d = strtod(str, &endptr);
	if (*endptr == 0)
	  success = 1;
	break;
      case UA_INT:
	a->i = strtol(str, &endptr, 10);
	if (*endptr == 0)
	  success = 1;
	break;
      case UA_STRING:
	a->s = strdup(str);
	if (a->s == NULL)
	  eperror("Out of memory allocating string option for song\n");
	success = 1;
	break;
      default:
	fprintf(stderr, "Unknown song option: %s\n", item);
	break;
      }

      if (success) {
	a->type = esvalueattrs[i].e;
	a->next = *attributelist;
	*attributelist = a;
      } else {
	fprintf(stderr, "Invalid song option: %s\n", item);
	free(a);
      }
      
      return 1;
    }
  }

  return 0;
}


struct eagleplayer *uade_analyze_file_format(const char *modulename,
					     struct uade_config *uc)
{
  struct eagleplayer *ep;
  int content;

  ep = analyze_file_format(&content, modulename, uc->basedir.name, uc->verbose);

  if (ep == NULL)
    return NULL;

  if (content)
    return ep;

  if (uc->magic_detection && content == 0)
    return NULL;

  if ((ep->flags & ES_CONTENT_DETECTION) != 0)
    return NULL;

  return ep;
}


static void uade_analyze_song(struct uade_song *us)
{
  struct eaglesong key;
  struct eaglesong *es;

  md5_from_buffer(us->md5, sizeof us->md5, us->buf, us->bufsize);

  if (strlen(us->md5) != ((sizeof key.md5) - 1)) {
    fprintf(stderr, "Invalid md5sum: %s\n", us->md5);
    exit(-1);
  }

  strlcpy(key.md5, us->md5, sizeof key.md5);

  es = bsearch(&key, songstore, nsongs, sizeof songstore[0], escompare);
  if (es != NULL) {
    us->flags |= es->flags;
    us->songattributes = es->attributes;
  }

  us->playtime = uade_find_playtime(us->md5);
  if (us->playtime <= 0)
    us->playtime = -1;
}


static int uade_find_playtime(const char *md5)
{
  struct contentchecksum key;
  struct contentchecksum *n;
  int playtime = 0;
  if (nccused == 0)
    return 0;
  strlcpy(key.md5, md5, sizeof key.md5);
  n = bsearch(&key, contentchecksums, nccused, sizeof contentchecksums[0], contentcompare);
  if (n != NULL)
    playtime = n->playtime;
  if (playtime < 0)
    playtime = 0;
  return playtime;
}


struct eagleplayer *uade_get_eagleplayer(const char *extension,
					 struct eagleplayerstore *ps)
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


static void md5_from_buffer(char *dest, size_t destlen,
			    uint8_t *buf, size_t bufsize)
{
  uint8_t md5[16];
  int ret;
  MD5_CTX ctx;
  MD5Init(&ctx);
  MD5Update(&ctx, buf, bufsize);
  MD5Final(md5, &ctx);
  ret = snprintf(dest, destlen, "%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",md5[0],md5[1],md5[2],md5[3],md5[4],md5[5],md5[6],md5[7],md5[8],md5[9],md5[10],md5[11],md5[12],md5[13],md5[14],md5[15]);
  if (ret >= destlen) {
    fprintf(stderr, "md5 buffer too short (%d/%zd)\n", ret, destlen);
    exit(-1);
  }
}


int uade_read_content_db(const char *filename)
{
  char line[256];
  FILE *f;
  nccused = 0;
  if (nccalloc == 0) {
    nccalloc = 16;
    contentchecksums = malloc(nccalloc * sizeof(struct contentchecksum));
    if (contentchecksums == NULL) {
      fprintf(stderr, "uade: No memory for content checksums\n");
      return 0;
    }
  }
  if ((f = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "uade: Can not find %s\n", filename);
    return 0;
  }

  while (fgets(line, sizeof line, f)) {
    long playtime;
    int i;
    char *eptr;
    if (line[0] == '#')
      continue;
    for (i = 0; i < 32; i++) {
      if (line[i] == 0 || !isxdigit(line[i]))
	break;
    }
    if (i != 32)
      continue;
    if (line[32] != ' ')
      continue;
    line[32] = 0;
    if (line[33] == '\n' || line[33] == 0)
      continue;
    playtime = strtol(&line[33], &eptr, 10);
    if (*eptr == '\n' || *eptr == 0) {
      if (playtime > 0)
	uade_add_playtime(line, playtime, 0);
    }
  }
  fclose(f);
  qsort(contentchecksums, nccused, sizeof contentchecksums[0], contentcompare);
  ccmodified = 0;

  /* fprintf(stderr, "uade: Read content database with %zd entries\n", nccused); */
  return 1;
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

	p->extensions = malloc((p->nextensions + 1) * sizeof(p->extensions[0]));
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

	continue;
      }

      if (strncasecmp(items[i], "comment:", 7) == 0)
	break;

      if (parse_attribute(&p->attributelist, &p->flags, items[i], lineno))
	continue;

      fprintf(stderr, "Unrecognized option: %s\n", items[i]);
    }

    for (i = 0; items[i] != NULL; i++)
      free(items[i]);

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
  if (ps)
    free(ps->players);
  free(ps);
  if (f != NULL)
    fclose(f);
  return NULL;
}


int uade_read_song_conf(const char *filename)
{
  FILE *f;
  struct eaglesong *s;
  size_t allocated;
  size_t lineno = 0;
  size_t i;

  if ((f = fopen(filename, "r")) == NULL)
    return 0;

  nsongs = 0;
  allocated = 16;
  songstore = calloc(allocated, sizeof songstore[0]);
  if (songstore == NULL)
    eperror("No memory for song store.");

  while (1) {
    char **items;
    size_t nitems;

    if ((items = split_line(&nitems, &lineno, f, WS_DELIMITERS)) == NULL)
      break;

    assert(nitems > 0);

    if (nsongs == allocated) {
      allocated *= 2;
      songstore = realloc(songstore, allocated * sizeof(songstore[0]));
      if (songstore == NULL)
	eperror("No memory for players.");
    }

    s = &songstore[nsongs];
    nsongs++;

    memset(s, 0, sizeof s[0]);

    if (strncasecmp(items[0], "md5=", 4) != 0) {
      fprintf(stderr, "Line %zd must begin with md5= in %s\n", lineno, filename);
      free(items);
      continue;
    }
    if (strlcpy(s->md5, items[0] + 4, sizeof s->md5) != ((sizeof s->md5) - 1)) {
      fprintf(stderr, "Line %zd in %s has too long an md5sum.\n", lineno, filename);
      free(items);
      continue;
    }

    for (i = 1; i < nitems; i++) {
      if (strncasecmp(items[i], "comment:", 7) == 0)
	break;
      if (parse_attribute(&s->attributes, &s->flags, items[i], lineno))
	continue;
      fprintf(stderr, "song option %s is invalid\n", items[i]);
    }

    for (i = 0; items[i] != NULL; i++)
      free(items[i]);

    free(items);
  }

  fclose(f);

  /* Sort MD5 sums for binary searching songs */
  qsort(songstore, nsongs, sizeof songstore[0], escompare);
  return 1;
}


void uade_save_content_db(const char *filename)
{
  FILE *f;
  size_t i;
  if (ccmodified == 0)
    return;
  if ((f = fopen(filename, "w")) == NULL) {
    fprintf(stderr, "uade: Can not write content db: %s\n", filename);
    return;
  }
  for (i = 0; i < nccused; i++)
    fprintf(f, "%s %u\n", contentchecksums[i].md5, (unsigned int) contentchecksums[i].playtime);
  fclose(f);
  fprintf(stderr, "uade: Saved %zd entries into content db.\n", nccused);
}


/* Compare function for bsearch() and qsort() to sort eagleplayers with
   respect to name extension. */
static int ufcompare(const void *a, const void *b)
{
  const struct eagleplayermap *ua = a;
  const struct eagleplayermap *ub = b;
  return strcasecmp(ua->extension, ub->extension);
}


void uade_unalloc_song(struct uade_song *us)
{
  free(us->buf);
  us->buf = NULL;
  free(us);
}


int uade_update_song_conf(const char *songconfin, const char *songconfout,
			  const char *songname, const char *options)
{
  int ret;
  int fd;
  char md5[33];
  void *mem = NULL;
  size_t filesize;
  int found = 0;
  size_t inputsize;
  uint8_t *input;
  uint8_t *inputptr;
  uint8_t *outputptr;
  size_t inputoffs;
  char newline[256];
  size_t i;
  int need_newline = 0;

  if (strlen(options) > 128) {
    fprintf(stderr, "Too long song.conf options.\n");
    return 0;
  }

  fd = open(songconfout, O_RDWR);
  if (fd < 0) {
    if (errno == ENOENT) {
      fd = open(songconfout, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
      if (fd < 0)
	return 0;
    } else {
      return 0;
    }
  }

  ret = lockf(fd, F_LOCK, 0);
  if (ret) {
    fprintf(stderr, "uade: Unable to to lock file: %s\n", strerror(errno));
    atomic_close(fd);
    return 0;
  }

  input = atomic_read_file(&inputsize, songconfin);
  if (input == NULL) {
    fprintf(stderr, "Can not read song.conf: %s\n", songconfin);
    atomic_close(fd); /* closes the lock too */
    return 0;
  }

  mem = realloc(input, inputsize + strlen(options) + strlen(songname) + 64);
  if (mem == NULL) {
    fprintf(stderr, "Can not realloc the input file buffer for song.conf.\n");
    free(input);
    atomic_close(fd); /* closes the lock too */
    return 0;
  }
  input = mem;

  mem = atomic_read_file(&filesize, songname);
  if (mem == NULL)
    goto error;

  md5_from_buffer(md5, sizeof md5, mem, filesize);

  inputptr = outputptr = input;
  inputoffs = 0;

  while (inputoffs < inputsize) {
    if (inputptr[0] == '#')
      goto copyline;

    if ((inputoffs + 37) >= inputsize)
      goto copyline;

    if (strncasecmp(inputptr, "md5=", 4) != 0)
      goto copyline;

    if (strncasecmp(inputptr + 4, md5, 32) == 0) {
      if (found) {
	fprintf(stderr, "Warning: dupe entry in song.conf: %s (%s)\n"
		"Need manual resolving.\n", songname, md5);
	goto copyline;
      }
      found = 1;
      snprintf(newline, sizeof newline, "md5=%s\t%s\n", md5, options);

      /* Skip this line. It will be appended later to the end of the buffer */
      for (i = inputoffs; i < inputsize; i++) {
	if (input[i] == '\n') {
	  i = i + 1 - inputoffs;
	  break;
	}
      }
      if (i == inputsize) {
	i = inputsize - inputoffs;
	found = 0;
	need_newline = 1;
      }
      inputoffs += i;
      inputptr += i;
      continue;
    }

  copyline:
    /* Copy the line */
    for (i = inputoffs; i < inputsize; i++) {
      if (input[i] == '\n') {
	i = i + 1 - inputoffs;
	break;
      }
    }
    if (i == inputsize) {
      i = inputsize - inputoffs;
      need_newline = 1;
    }
    memmove(outputptr, inputptr, i);
    inputoffs += i;
    inputptr += i;
    outputptr += i;
  }

  if (need_newline) {
    snprintf(outputptr, 2, "\n");
    outputptr += 1;
  }

  /* there is enough space */
  ret = snprintf(outputptr, PATH_MAX + 256, "md5=%s\t%s\tcomment %s\n", md5, options, songname);
  outputptr += ret;

  if (ftruncate(fd, 0)) {
    fprintf(stderr, "Can not truncate the file.\n");
    goto error;
  }

  /* Final file size */
  i = (size_t) (outputptr - input);

  if (atomic_write(fd, input, i) < i)
    fprintf(stderr, "Unable to write file contents back. Data loss happened. CRAP!\n");

 error:
  atomic_close(fd); /* Closes the lock too */
  free(input);
  free(mem);
  return 1;
}
