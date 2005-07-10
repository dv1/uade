
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <uadeformats.h>

#define UADE_PLAYER_CERTAIN_END (1)


struct uadeformat {
  char *extension;
  char *playername;
  int attributes;
};


static void attribute_match(int *attribute, char *str)
{
  if (strcmp(str, "certain_end") == 0) {
    *attribute |= UADE_PLAYER_CERTAIN_END;
  } else {
    fprintf(stderr, "uadeformats: unknown attribute: %s\n", str);
  }
}


static int skip_ws(const char *s, int pos)
{
  while (isspace(s[pos]) && s[pos] != 0)
    pos++;
  if (s[pos] == 0)
    return -1;
  return pos;
}


static int skip_nws(const char *s, int pos)
{
  while (!isspace(s[pos]) && s[pos] != 0)
    pos++;
  if (s[pos] == 0)
    return -1;
  return pos;
}


void *uade_read_uadeformats(char *filename)
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
  size_t n;

  if (f == NULL)
    return NULL;

  n = 1;
  while (fgets(line, sizeof(line), f) != NULL)
    n++;
  if (n == 0) {
    fprintf(stderr, "teh incredible thing happened with uadeformats file\n");
    exit(-1);
  }

  formats = malloc(n * sizeof(formats[0]));
  if (formats == NULL) {
    fprintf(stderr, "no memory for uadeformats file\n");
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET)) {
    fprintf(stderr, "fseek failed\n");
    fclose(f);
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
    strlcpy(extension, &line[pos], sizeof(extension));
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
      fprintf(stderr, "uadeformats: no memory\n");
      fclose(f);
      return NULL;
    }
  }
  fclose(f);
  return formats;
}


int uade_get_playername(const char *pre, void *formats)
{
  assert(0);
}
