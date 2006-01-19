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

/* Get the info out of the protracker module data*/
static void process_ptk_mod(char *credits, int credits_len, int inst,
			    unsigned char *buf, int len, char tmpstr[])
{
  int i;

  if (inst == 31) {
    if (len >= 0x43c) {

      snprintf(tmpstr, 256,"max positions:  %d\n", buf[0x3b6]);
      strlcat(credits, tmpstr, credits_len);

    }
  } else {
    if (len >= 0x1da) {
      snprintf(tmpstr, 256,"max positions:  %d\n", buf[0x1d6]);
      strlcat(credits, tmpstr, credits_len);
    }
  }

  if (len >= (0x14 + inst * 0x1e)) {
    for (i = 0; i < inst; i++) {
      if (i < 10) {
        snprintf(tmpstr, 256,"\ninstr #0%d:  ", i);
        strlcat(credits, tmpstr, credits_len);
      } else {
        snprintf(tmpstr, 256,"\ninstr #%d:  ", i);
        strlcat(credits, tmpstr, credits_len);
      }
      snprintf(tmpstr, 22,buf + 0x14 + (i * 0x1e));
      strlcat(credits, tmpstr, credits_len);
    }
  }
}

/* 
 * Get the info out of the Deltamusic 2 module data
 */
static void process_dm2_mod(char *credits, int credits_len,
			    unsigned char *buf, char tmpstr[])
{
  snprintf(tmpstr, 256,"\nRemarks:\n%s", buf + 0x148);
  strlcat(credits, tmpstr, credits_len);
}


static int process_module(char *credits, size_t credits_len,char *filename)
{
  FILE *modfile;
  struct stat st;
  int modfilelen;
  unsigned char *buf;
  char pre[11];
  char tmpstr[256];
  int ret;

  if (!(modfile = fopen(filename, "rb")))
    return 0;

  fstat(fileno(modfile), &st);
  modfilelen = st.st_size;

  if (!(buf = malloc(modfilelen))) {
    fprintf(stderr, "can't allocate mem");
    fclose(modfile);
    return 0;
  }

  ret = fread(buf, 1, modfilelen, modfile); /*Reading file over network? Bad
                                              luck :) we read the truth but the
                                              whole truth*/
  fclose(modfile);

  if (ret < modfilelen) {
    fprintf(stderr, "uade: song info could not read %s fully\n",
	    filename);
    free(buf);
    return 0;
  }

  snprintf(tmpstr, 256,"UADE2 MODINFO:\n\nFile name:\t%s\nFile length:\t%d bytes\n", filename, modfilelen);
  strlcpy (credits, tmpstr,credits_len);

  /* here we go */
  uade_filemagic(buf,modfilelen,pre,modfilelen); /*get filetype in pre*/

  snprintf(tmpstr, 256,"File prefix:\t%s.*\n", pre);
  strlcat (credits, tmpstr,credits_len);

  /* DM2 */
  if (strcasecmp(pre, "DM2") == 0) {
    process_dm2_mod(credits, credits_len, buf, tmpstr);	/*DM2 */

  } else if ((strcasecmp(pre, "MOD15") == 0) ||
	     (strcasecmp(pre, "MOD15_UST") == 0) ||
	     (strcasecmp(pre, "MOD15_MST") == 0) ||
	     (strcasecmp(pre, "MOD15_ST-IV") == 0)) {
    /*MOD15 */
    process_ptk_mod(credits, credits_len, 15, buf, modfilelen, tmpstr);

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
    process_ptk_mod(credits, credits_len, 31, buf, modfilelen,tmpstr);
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

