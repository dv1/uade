#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <songinfo.h>


static void asciiline(char *dst, unsigned char *buf)
{
  int i, c;
  for (i = 0; i < 16; i++) {
    c = buf[i];
    if (isgraph(c) || c == ' ') {
      dst[i] = c;
    } else {
      dst[i] = '.';
    }
  }
  dst[i] = 0;
}


/* Returns zero on success, non-zero otherwise. */
int uade_song_info(char *info, char *filename, size_t maxlen)
{
  FILE *f = fopen(filename, "rb");
  size_t rb, ret;
  uint8_t buf[1024];

  assert(maxlen >= 8192);

  if (f == NULL)
    return 0;

  rb = 0;
  while (rb < sizeof buf) {
    ret = fread(&buf[rb], 1, sizeof(buf) - rb, f);
    if (ret == 0)
      break;
    rb += ret;
  }

  if (rb > 0) {
    size_t roff = 0;
    size_t woff = 0;

    while (roff < rb) {
      int iret;

      if (woff >= maxlen)
	break;

      if (woff + 32 >= maxlen) {
	strcpy(&info[woff], "\nbuffer overflow...\n");
	woff += strlen(&info[woff]);
	break;
      }

      iret = snprintf(&info[woff], maxlen - woff, "%.3zx:  ", roff);
      assert(iret > 0);
      woff += iret;

      if (woff >= maxlen)
	break;

      if (roff + 16 > rb) {
	snprintf(&info[woff], maxlen - woff, "Aligned line  ");
      } else {
	char dbuf[17];
	asciiline(dbuf, &buf[roff]);
	iret = snprintf(&info[woff], maxlen - woff,
			"%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x  %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x  |%s|",
			buf[roff + 0], buf[roff + 1], buf[roff + 2], buf[roff + 3],
			buf[roff + 4], buf[roff + 5], buf[roff + 6], buf[roff + 7],
			buf[roff + 8], buf[roff + 9], buf[roff + 10], buf[roff + 11],
			buf[roff + 12], buf[roff + 13], buf[roff + 14], buf[roff + 15],
			dbuf);
	assert(iret > 0);
	woff += iret;
      }

      if (woff >= maxlen)
	break;

      iret = snprintf(&info[woff], maxlen - woff, "\n");
      woff += iret;

      roff += 16;
    }

    if (woff >= maxlen)
      woff = maxlen - 1;
    info[woff] = 0;
  }

  fclose(f);
  return rb == 0;
}

