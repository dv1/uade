#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <strlrep.h>
#include <songinfo.h>
#include <amifilemagic.h>

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


static int hexdump(char *info, size_t maxlen, char *filename)
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
	iret = snprintf(&info[woff], maxlen - woff, "Aligned line  ");
	assert(iret > 0);
	woff += iret;

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


static uint16_t read_be_u16(uint8_t *ptr)
{
  uint16_t x = ptr[1] + (ptr[0] << 8);
  return x;
}


static int string_checker(char *str, size_t off, size_t maxoff)
{
  assert(maxoff > 0);
  while (off < maxoff) {
    if (*str == 0)
      return 1;
    off++;
    str++;
  }
  return 0;
}


/* Get the info out of the AHX  module data*/
static void process_ahx_mod(char *credits, size_t credits_len,
			    unsigned char *buf, size_t len)
{
  int i;
  size_t offset;
  char tmpstr[256];

  if (len < 13)
    return;

  offset = read_be_u16(buf + 4);

  if (offset >= len)
    return;

  if (!string_checker(buf, offset, len))
    return;

  snprintf(tmpstr, sizeof tmpstr, "\nSongtitle:\t%s\n", buf + offset);
  strlcat(credits, tmpstr, credits_len);

  for (i = 0; i < buf[12]; i++) {
    if (!string_checker(buf, offset, len))
      break;
    offset = offset + 1 + strlen(buf + offset);
    if (offset < len) {
      snprintf(tmpstr, 256,"\n\t\t%s", buf + offset);
      strlcat(credits, tmpstr, credits_len);
    }
  }
}

/* Get the info out of the protracker module data*/
static void process_ptk_mod(char *credits, size_t credits_len, int inst,
			    uint8_t *buf, size_t len)
{
  int i;
  char tmpstr[256];

  if (!string_checker(buf, 0, len))
    return;

  snprintf(tmpstr, 32, "\nSongtitle:\t%s\n", buf);
  strlcat(credits, tmpstr, credits_len);

  if (inst == 31) {
    if (len >= 0x43c) {
      snprintf(tmpstr, sizeof tmpstr, "max positions:  %d\n", buf[0x3b6]);
      strlcat(credits, tmpstr, credits_len);
    }
  } else {
    if (len >= 0x1da) {
      snprintf(tmpstr, sizeof tmpstr, "max positions:  %d\n", buf[0x1d6]);
      strlcat(credits, tmpstr, credits_len);
    }
  }

  if (len >= (0x14 + inst * 0x1e)) {
    for (i = 0; i < inst; i++) {
      if (!string_checker(buf, 0x14 + i * 0x1e, len))
	break;
      snprintf(tmpstr, sizeof tmpstr,"\ninstr #%.2d:\t", i);
      strlcat(credits, tmpstr, credits_len);
      snprintf(tmpstr, 22, buf + 0x14 + (i * 0x1e));
      strlcat(credits, tmpstr, credits_len);
    }
  }
}

/* 
 * Get the info out of the Deltamusic 2 module data
 */
static void process_dm2_mod(char *credits, size_t credits_len,
			    unsigned char *buf, size_t len)
{
  char tmpstr[256];
  if (!string_checker(buf, 0x148, len))
    return;
  snprintf(tmpstr, sizeof tmpstr, "\nRemarks:\n%s", buf + 0x148);
  strlcat(credits, tmpstr, credits_len);
}


static int process_module(char *credits, size_t credits_len,char *filename)
{
  FILE *modfile;
  struct stat st;
  size_t modfilelen;
  unsigned char *buf;
  char pre[11];
  char tmpstr[256];
  size_t rb;

  if (!(modfile = fopen(filename, "rb")))
    return 0;

  if (fstat(fileno(modfile), &st))
    return 0;

  modfilelen = st.st_size;

  if ((buf = malloc(modfilelen)) == NULL) {
    fprintf(stderr, "can't allocate mem");
    fclose(modfile);
    return 0;
  }

  rb = 0;
  while (rb < modfilelen) {
    size_t ret = fread(&buf[rb], 1, modfilelen - rb, modfile);
    if (ret == 0)
      break;
    rb += ret;
  }

  fclose(modfile);

  if (rb < modfilelen) {
    fprintf(stderr, "uade: song info could not read %s fully\n", filename);
    free(buf);
    return 0;
  }

  snprintf(tmpstr, sizeof tmpstr, "UADE2 MODINFO:\n\nFile name:\t%s\nFile length:\t%zd bytes\n", filename, modfilelen);
  strlcpy (credits, tmpstr,credits_len);

  /* here we go */
  uade_filemagic(buf,modfilelen,pre,modfilelen); /*get filetype in pre*/

  snprintf(tmpstr, sizeof tmpstr, "File prefix:\t%s.*\n", pre);
  strlcat (credits, tmpstr,credits_len);

  if (strcasecmp(pre, "DM2") == 0) {
  /* DM2 */
    process_dm2_mod(credits, credits_len, buf, modfilelen);	/*DM2 */

  } else if ((strcasecmp(pre, "AHX") == 0) ||
	     (strcasecmp(pre, "THX") == 0)) {
    /* AHX */
    process_ahx_mod(credits, credits_len, buf, modfilelen);

  } else if ((strcasecmp(pre, "MOD15") == 0) ||
	     (strcasecmp(pre, "MOD15_UST") == 0) ||
	     (strcasecmp(pre, "MOD15_MST") == 0) ||
	     (strcasecmp(pre, "MOD15_ST-IV") == 0)) {
    /*MOD15 */
    process_ptk_mod(credits, credits_len, 15, buf, modfilelen);

  } else if ((strcasecmp(pre, "MOD") == 0) ||

	     (strcasecmp(pre, "MOD_DOC") == 0) ||
	     (strcasecmp(pre, "MOD_NTK1") == 0) ||
	     (strcasecmp(pre, "MOD_NTK2") == 0) ||
	     (strcasecmp(pre, "MOD_FLT4") == 0) ||
	     (strcasecmp(pre, "MOD_FLT8") == 0) ||
	     (strcasecmp(pre, "MOD_ADSC4") == 0) ||
	     (strcasecmp(pre, "MOD_ADSC8") == 0) ||
	     (strcasecmp(pre, "MOD_PTKCOMP") == 0) ||
	     (strcasecmp(pre, "MOD_NTKAMP") == 0) ||
  	     (strcasecmp(pre, "PPK") == 0) ||
	     (strcasecmp(pre, "MOD_PC") == 0) ||
	     (strcasecmp(pre, "ICE") == 0) ||
	     (strcasecmp(pre, "ADSC") == 0)) {
    /*MOD*/
    process_ptk_mod(credits, credits_len, 31, buf, modfilelen);
  }
  return 0;
}

/* Returns zero on success, non-zero otherwise. */
int uade_song_info(char *info, size_t maxlen, char *filename,
		   enum song_info_type type)
{
  switch (type) {
  case UADE_MODULE_INFO:
    return process_module(info,maxlen,filename);
  case UADE_HEX_DUMP_INFO:
    return hexdump(info, maxlen, filename);
  default:
    fprintf(stderr, "Illegal info requested.\n");
    exit(-1);
  }
  return 0;
}

