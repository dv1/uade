#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "songdb.h"
#include "md5.h"
#include "unixatomic.h"
#include "strlrep.h"


#define WS_DELIMITERS " \t\n"


#define eserror(fmt, args...) do { fprintf(stderr, "song.conf error on line %zd: " fmt "\n", lineno, ## args); exit(-1); } while (0)


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


static int nsongs;
static struct eaglesong *songstore;


/* Compare function for bsearch() and qsort() to sort songs with respect
   to their md5sums */
static int contentcompare(const void *a, const void *b)
{
  return strcasecmp(((struct contentchecksum *) a)->md5,
		    ((struct contentchecksum *) b)->md5);
}


static int escompare(const void *a, const void *b)
{
  return strcasecmp(((struct eaglesong *) a)->md5,
		    ((struct eaglesong *) b)->md5);
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


void uade_analyze_song_from_songdb(struct uade_song *us)
{
  struct eaglesong key;
  struct eaglesong *es;

  uade_md5_from_buffer(us->md5, sizeof us->md5, us->buf, us->bufsize);

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


int uade_find_playtime(const char *md5)
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


void uade_md5_from_buffer(char *dest, size_t destlen,
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


int uade_open_and_lock(const char *filename, int create)
{
  int fd, ret;
  fd = open(filename, O_RDWR);
  if (fd < 0) {
    if (errno == ENOENT && create) {
      fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
      if (fd < 0)
	return -1;
    } else {
      return -1;
    }
  }

  ret = lockf(fd, F_LOCK, 0);
  if (ret) {
    fprintf(stderr, "uade: Unable to lock song.conf: %s (%s)\n",
	    filename, strerror(errno));
    atomic_close(fd);
    return -1;
  }

  return fd;
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


int uade_read_song_conf(const char *filename)
{
  FILE *f = NULL;
  struct eaglesong *s;
  size_t allocated;
  size_t lineno = 0;
  size_t i;
  int fd;

  fd = uade_open_and_lock(filename, 1);
  /* open_and_lock() may fail without harm (it's actually supposed to fail
     if the process does not have lock (write) permissions to the song.conf
     file */

  f = fopen(filename, "r");
  if (f == NULL)
    goto error;

  nsongs = 0;
  allocated = 16;
  songstore = calloc(allocated, sizeof songstore[0]);
  if (songstore == NULL)
    eserror("No memory for song store.");

  while (1) {
    char **items;
    size_t nitems;

    if ((items = uade_split_line(&nitems, &lineno, f, WS_DELIMITERS)) == NULL)
      break;

    assert(nitems > 0);

    if (nsongs == allocated) {
      allocated *= 2;
      songstore = realloc(songstore, allocated * sizeof(songstore[0]));
      if (songstore == NULL)
	eserror("No memory for players.");
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
      if (uade_parse_attribute(&s->attributes, &s->flags, items[i], lineno))
	continue;
      fprintf(stderr, "song option %s is invalid\n", items[i]);
    }

    for (i = 0; items[i] != NULL; i++)
      free(items[i]);

    free(items);
  }

  fclose(f);

  /* we may not have the file locked */
  if (fd >= 0)
    atomic_close(fd); /* lock is closed too */

  /* Sort MD5 sums for binary searching songs */
  qsort(songstore, nsongs, sizeof songstore[0], escompare);
  return 1;

 error:
  if (f)
    fclose(f);
  if (fd >= 0)
    atomic_close(fd);
  return 0;
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
  char *input, *inputptr, *outputptr;
  size_t inputoffs;
  char newline[256];
  size_t i;
  int need_newline = 0;

  if (strlen(options) > 128) {
    fprintf(stderr, "Too long song.conf options.\n");
    return 0;
  }

  fd = uade_open_and_lock(songconfout, 1);

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

  uade_md5_from_buffer(md5, sizeof md5, mem, filesize);

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
