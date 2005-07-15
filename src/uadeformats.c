/* UADE
 *
 * Copyright 2005 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * Loads contents of 'uadeformats' file into a sorted list, and does binary
 * searches into it. The format of the file should be obvious from the
 * example file provided with UADE distribution.
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) in any you
 * want in your projects.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <strlrep.h>

#include <uadeformats.h>

#define UADE_PLAYER_CERTAIN_END (1)


struct uadeformat {
  char *extension;
  char *playername;
  int attributes;
};


static int ufcompare(const void *a, const void *b);


static void attribute_match(int *attribute, char *str)
{
  if (strcmp(str, "certain_end") == 0) {
    *attribute |= UADE_PLAYER_CERTAIN_END;
  } else {
    fprintf(stderr, "uadeformats: unknown attribute: %s\n", str);
  }
}


/* skips whitespace characters from string 's'. returns -1 if end of string is
   reached before a non-whitespace character */
static int skip_ws(const char *s, int pos)
{
  while (isspace(s[pos]) && s[pos] != 0)
    pos++;
  if (s[pos] == 0)
    return -1;
  return pos;
}


/* skips non-whitespace characters from string 's'. returns -1 if end of string
   is reached before a whitespace character */
static int skip_nws(const char *s, int pos)
{
  while (!isspace(s[pos]) && s[pos] != 0)
    pos++;
  if (s[pos] == 0)
    return -1;
  return pos;
}



char *uade_get_playername(const char *extension, void *formats, int nformats)
{
  struct uadeformat *uf = formats;
  struct uadeformat *f;
  struct uadeformat key = {.extension = (char *) extension};

  f = bsearch(&key, uf, nformats, sizeof(uf[0]), ufcompare);
  if (f == NULL)
    return NULL;
  return f->playername;
}


/* Reads uadeformats file line by line. collect following data from each line:

   - extension string
   - playername string (matches the filename in players/ dir)
   - possible attribute words (properties of players)

   All the data is put into an 'struct uadeformat' array, and the array
   is sorted. Don't bitch me about worst case complexity of O(n^2). Where
   is that guaranteed O(n*log(n)) sort in C library???

   Later the sorted array is used to search extension strings rapidly with
   binary search (C libs bsearch). */
void *uade_read_uadeformats(int *nformats, char *filename)
{
  FILE *f = fopen(filename, "r");
  char line[256];
  char extension[16];
  char playername[256];
  int pos;
  int next;
  int len;
  int attributes;
  struct uadeformat *formats;
  size_t n, orgn;

  *nformats = -1;

  if (f == NULL)
    return NULL;

  orgn = 1;
  while (fgets(line, sizeof(line), f) != NULL)
    orgn++;
  if (orgn == 0) {
    fprintf(stderr, "teh incredible thing happened with uadeformats file\n");
    exit(-1);
  }

  formats = malloc(orgn * sizeof(formats[0]));
  if (formats == NULL) {
    fprintf(stderr, "no memory for uadeformats file\n");
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET)) {
    fprintf(stderr, "fseek failed\n");
    fclose(f);
    free(formats);
    return NULL;
  }

  n = 0;
  while (fgets(line, sizeof(line), f) != NULL) {
    if (line[0] == '#')
      continue;
    len = strlen(line);
    if (line[len - 1] == '\n')
      line[len - 1] = 0;
    pos = skip_ws(line, 0);
    if (pos < 0) {
      fprintf(stderr, "illegal line in format file: %s\n", line);
      continue;
    }
    /* get extension name */
    next = skip_nws(line, pos);
    if (next < 0) {
      fprintf(stderr, "illegal line in format file: %s\n", line);
      continue;
    }
    line[next] = 0;
    if (strlcpy(extension, &line[pos], sizeof(extension)) >= sizeof(extension)) {
      fprintf(stderr, "too long an extension: %s\n", &line[pos]);
      exit(-1);
    }
    pos = next + 1;

    pos = skip_ws(line, pos);
    if (pos < 0) {
      fprintf(stderr, "illegal line in format file: %s\n", line);
      continue;
    }

    attributes = 0;

    /* get playername */
    next = skip_nws(line, pos);
    if (next < 0) {
      strlcpy(playername, &line[pos], sizeof(playername));
      goto gotit;
    }
    line[next] = 0;
    strlcpy(playername, &line[pos], sizeof(playername));
    pos = next + 1;

    while (1) {
      pos = skip_ws(line, pos);
      if (pos < 0)
	break;
      next = skip_nws(line, pos);
      if (next < 0) {
	attribute_match(&attributes, &line[pos]);
	break;
      }
      line[next] = 0;
      attribute_match(&attributes, &line[pos]);
      pos = next + 1;
    }

  gotit:
    formats[n].extension = strdup(extension);
    formats[n].playername = strdup(playername);
    formats[n].attributes = attributes;
    if (formats[n].extension == NULL || formats[n].playername == NULL) {
      fprintf(stderr, "uadeformats: no memory. not freeing what was allocated. haha!\n");
      fclose(f);
      return NULL;
    }
    n++;
    /* if formats file grows suddenly, n can grow beyond orgn, but we don't
       want to run over unallocated memory so we do a comparison here */
    if (n == orgn)
      break;
  }
  fclose(f);

  *nformats = n;

  if (n == 0) {
    free(formats);
    return NULL;
  }

  qsort(formats, n, sizeof(formats[0]), ufcompare);

  return formats;
}


/* compare function for qsort() */
static int ufcompare(const void *a, const void *b)
{
  const struct uadeformat *ua = a;
  const struct uadeformat *ub = b;
  return strcasecmp(ua->extension, ub->extension);
}
