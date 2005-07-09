/* UADE - Unix Amiga Delitracker Emulator
 * Copyright 2000-2005, Heikki Orsila <heikki.orsila@iki.fi>
 * See http://uade.ton.tut.fi
 */

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <limits.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "events.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "debug.h"
#include "gensound.h"
#include "cia.h"
#include "sd-sound.h"

#include "../config.h"

#include "uade.h"
#include "amigamsg.h"
#include "strlrep.h"
#include "players.h"
#include "uadecontrol.h"


#define OPTION_HELP (1)
#define OPTION_ILLEGAL_PARAMETERS (2)
#define OPTION_NO_SONGS (3)

static int uade_calc_reloc_size(uae_u32 *src, uae_u32 *end);
static int uade_get_long(int addr);
static void uade_interaction(int wait_for);
static void uade_put_long(int addr,int val);
static void uade_reset_counters(void);
static int uade_safe_load(int dst, FILE *file, int maxlen);


static const int SCORE_MODULE_ADDR   = 0x100;
static const int SCORE_MODULE_LEN    = 0x104;
static const int SCORE_PLAYER_ADDR   = 0x108;
static const int SCORE_RELOC_ADDR    = 0x10C;
static const int SCORE_USER_STACK    = 0x110;
static const int SCORE_SUPER_STACK   = 0x114;
static const int SCORE_FORCE         = 0x118;
static const int SCORE_SET_SUBSONG   = 0x11c;
static const int SCORE_SUBSONG       = 0x120;
static const int SCORE_NTSC          = 0x124;
static const int SCORE_MODULE_NAME_ADDR = 0x128;
static const int SCORE_HAVE_SONGEND  = 0x12C;
static const int SCORE_POSTPAUSE     = 0x180;
static const int SCORE_PREPAUSE      = 0x184;
static const int SCORE_DELIMON       = 0x188;
static const int SCORE_EXEC_DEBUG    = 0x18C;
static const int SCORE_VOLUME_TEST   = 0x190;
static const int SCORE_DMA_WAIT      = 0x194;
static const int SCORE_MODULECHANGE  = 0x198;

static const int SCORE_INPUT_MSG     = 0x200;
static const int SCORE_MIN_SUBSONG   = 0x204;
static const int SCORE_MAX_SUBSONG   = 0x208;
static const int SCORE_CUR_SUBSONG   = 0x20C;

static const int SCORE_OUTPUT_MSG    = 0x300;


static int disable_modulechange = 0;
struct uade_song song;
static int uade_big_endian;
static int uade_execdebugboolean = 0;
int uade_debug = 0;
static int uade_dmawait = 0;
static int uade_do_panning = 0;
static int uade_highmem = 0x200000;
static float uade_pan_value = 1.0f;
int uade_read_size = 0;
int uade_reboot;
static int uade_speed_hack = 0;
int uade_swap_output_bytes = 0;
int uade_time_critical = 0;
static int uade_vsync_counter;
static int uade_zero_sample_count;
/* contains uade's command line name */
static char *uadecmdlinename = 0;
static int voltestboolean = 0;


static int uade_calc_reloc_size(uae_u32 *src, uae_u32 *end) {
  uae_u32 offset;
  int i, nhunks;

  if (ntohl(*src) != 0x000003f3)
    return 0;
  src++;

  if (src >= end)
    return 0;
  if (ntohl(*src))
    return 0;
  src++;

  if (src >= end)
    return 0;
  nhunks = ntohl(*src); /* take number of hunks */
  if (nhunks <= 0)
    return 0;
  src += 3;          /* skip number of hunks, and first & last hunk indexes */

  offset = 0;

  for (i = 0; i < nhunks; i++) {

    if (src >= end)
      return 0;
    offset += 4 * (ntohl(*src) & 0x3FFFFFFF);
    src++;
  }
  if (((int) offset) <= 0 || ((int) offset) >= uade_highmem)
    return 0;
  return ((int) offset);
}


/* last part of the audio system pipeline */
void uade_check_sound_buffers(int bytes)
{
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *uc = (struct uade_msg *) space;

  /* effects */
  if (uade_do_panning) {
    if (currprefs.stereo) {
      int to_frames_divisor = sound_bytes_per_sample * 2;
      assert(0);
      /* uade_effect_pan((short *) sndbuffer, sndbufsize / to_frames_divisor, bytes_per_sample, uade_pan_value); */
    }
  }

  /* silence testing */
  uade_test_sound_block((void *) sndbuffer, bytes);

  /* transmit in big endian format, so swap if little endian */
  if (uade_big_endian == 0)
    uade_swap_buffer_bytes(sndbuffer, bytes);

  uc->msgtype = UADE_REPLY_DATA;
  uc->size = bytes;
  memcpy(uc->data, sndbuffer, bytes);
  uade_send_message(uc);

  uade_read_size -= bytes;
  assert(uade_read_size >= 0);

  if (uade_read_size == 0)
    uade_receive_control(1);
  /* uade_receive_control(0); */
}


void uade_receive_control(int block)
{
  int no_more_commands;
  uint8_t space[UADE_MAX_MESSAGE_SIZE];
  struct uade_msg *uc = (struct uade_msg *) space;
  int ret;

  assert(block != 0);

  ret = uade_receive_message(uc, sizeof(space));
  if (ret == 0) {
    fprintf(stderr, "no more input. exiting succesfully.\n");
    exit(0);
  } else if (ret < 0) {
    fprintf(stderr, "error on input. exiting with error\n");
    exit(-1);
  }

  no_more_commands = 0;
  while (no_more_commands == 0) {
    switch (uc->msgtype) {
    case UADE_COMMAND_READ:
      if (uade_read_size != 0) {
	fprintf(stderr, "read not allowed when uade_read_size > 0\n");
	exit(-1);
      }
      if (uc->size != 4) {
	fprintf(stderr, "illegal size on read command\n");
	exit(-1);
      }
      uade_read_size = ntohl(* (uint32_t *) uc->data);
      if (uade_read_size == 0 || uade_read_size > MAX_SOUND_BUF_SIZE) {
	fprintf(stderr, "illegal read size: %d\n", uade_read_size);
	exit(-1);
      }
      no_more_commands = 1;
      break;
    case UADE_COMMAND_REBOOT:
      uade_reboot = 1;
      no_more_commands = 1;
      break;
    default:
      fprintf(stderr, "error: received command %d\n", uc->msgtype);
      exit(-1);
    }
  }
}


static FILE *uade_open_amiga_file(const char *filename)
{
  return fopen(filename, "r");
}


void uade_option(int argc, char **argv)
{
  int i, j, no_more_opts;
  char **s_argv;
  int s_argc;
  int cfg_loaded = 0;
  char optionsfile[UADE_PATH_MAX];
  int ret;

  /* network byte order is the big endian order */
  uade_big_endian = htonl(0x1234) == 0x1234;

  uadecmdlinename = strdup(argv[0]);

  memset(&song, 0, sizeof(song));

  no_more_opts = 0;

  s_argv = malloc(sizeof(argv[0]) * (argc + 1));
  if (!s_argv) {
    fprintf (stderr, "uade: out of memory for command line parsing\n");
    exit(-1);
  }
  s_argc = 0;
  s_argv[s_argc++] = argv[0];

  for (i = 1; i < argc;) {

    j = i;

    /* if argv[i] begins with '-', see if it is a switch that we should
       handle here. */
    
    if (argv[i][0] == '-') {

      if (!strcmp(argv[i], "-debug") || !strcmp(argv[i], "-d")) {
	uade_debug = 1;
	i++;

      } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") || !strcmp(argv[i], "-help")) {
	uade_print_help(OPTION_HELP);
	exit(0);

      } else if (!strcmp(argv[i], "-i")) {
	if ((i + 1) >= argc) {
	  fprintf(stderr, "%s parameter missing\n", argv[i]);
	  uade_print_help(OPTION_ILLEGAL_PARAMETERS);
	  exit(-1);
	}
	uade_set_input_source(argv[i + 1]);
	i += 2;

      } else if (!strcmp(argv[i], "-o")) {
	if ((i + 1) >= argc) {
	  fprintf(stderr, "%s parameter missing\n", argv[i]);
	  uade_print_help(OPTION_ILLEGAL_PARAMETERS);
	  exit(-1);
	}
	uade_set_output_destination(argv[i + 1]);
	i += 2;

      } else if (!strcmp(argv[i], "--")) {
	for (i = i + 1; i < argc ; i++)
	  s_argv[s_argc++] = argv[i];
	break;
      }
    }

    if (i == j) {
      s_argv[s_argc++] = argv[i];
      i++;
    }
  }
  s_argv[s_argc] = NULL;

  ret = uade_receive_string(optionsfile, UADE_COMMAND_CONFIG, sizeof(optionsfile));
  if (ret == 0) {
    fprintf(stderr, "no config file passed as a message\n");
    exit(-1);
  } else if (ret < 0) {
    fprintf(stderr, "illegal input (expected a config file)\n");
    exit(-1);
  }

  /* use the config file provided with a message, if '-config' option
     was not given */
  if (!cfg_loaded) {
    if (cfgfile_load (&currprefs, optionsfile) == 0) {
      fprintf(stderr, "couldn't load uaerc (%s)!\n", optionsfile);
      exit(-1);
    }
  }

  song.timeout = -1;		/* default timeout infinite */
  song.subsong_timeout = -1;	/* default per subsong timeout infinite */
  song.silence_timeout = -1;	/* default silence timeout infinite */

  free(s_argv);

  if (uade_debug)
    activate_debugger();

  uade_reboot = 1;
}


void uade_print_help(int problemcode)
{
  switch (problemcode) {
  case OPTION_HELP:
    /* just for printing help */
    break;
  case OPTION_ILLEGAL_PARAMETERS:
    fprintf(stderr, "Illegal parameters\n\n");
    break;
  case OPTION_NO_SONGS:
    fprintf(stderr, "No songs given as parameters\n\n");
    break;
  default:
    fprintf(stderr, "Unknown error\n");
    break;
  }
  fprintf(stderr, "UADE usage:\n");
  fprintf(stderr, " %s [OPTIONS]\n\n", uadecmdlinename);

  fprintf(stderr, " options:\n");
  fprintf(stderr, " -d\t\tSet debug mode\n");
  fprintf(stderr, " -h\t\tPrint help\n");
  fprintf(stderr, " -i file\t\tSet input source\n");
  fprintf(stderr, " -o file\t\tSet output destination\n");
  fprintf(stderr, "\n");
}


static int uade_safe_load_name(int vaddr, char *name, const char *expl,
			       int maxlen)
{
  int bytesread, status;
  FILE *file;
  file = fopen(name, "r");
  if (!file) {
    fprintf(stderr,"uade: couldn't load %s %s\n", expl, name);
    return 0;
  }
  bytesread = uade_safe_load(vaddr, file, maxlen);
  fclose(file);
  return bytesread;
}


/* this is called for each played song from newcpu.c/m68k_reset() */
void uade_reset(void)
{
  /* don't load anything under 0x1000 (execbase top at $1000) */
  int modnameaddr = 0x00400;
  int scoreaddr   = 0x01000;
  int userstack   = 0x08500;
  int superstack  = 0x08f00;
  int playeraddr  = 0x09000;
  int relocaddr   = 0x40000;   /* default, recalculate this */
  int modaddr     = 0x80000;   /* default, recalculate this */
  int len;
  FILE *file;
  int bytesread;

  const int maxcommand = 4096;
  uint8_t command[maxcommand];
  struct uade_msg *uc = (struct uade_msg *) command;

  int ret;

  /* IMPORTANT:
     It seems that certain players don't work totally reliably if memory
     contains trash from previous songs. To be certain that each song is
     played from the same initial state of emulator we clear the memory
     from 0x400 to 'uade_highmem' each time a new song is played */
  uade_highmem = 0;
  while (uade_highmem < 0x200000) {
    if (!valid_address(0, uade_highmem + 0x10000))
      break;
    uade_highmem += 0x10000;
  }
  if (uade_highmem < 0x80000) {
    fprintf(stderr, "fatal error: there must be at least 512 KiB of amiga memory (%d bytes found)\n", uade_highmem);
    exit(-1);
  }
  if (uade_highmem < 0x200000) {
    fprintf(stderr, ": warning: highmem == 0x%x (< 0x200000)!\n", uade_highmem);
  }
  memset(get_real_address(0), 0, uade_highmem);
  
 takenextsong:

  song.song_end_possible = 1;
  song.cur_subsong = song.min_subsong = song.max_subsong = 0;

  ret = uade_receive_string(song.scorename, UADE_COMMAND_SCORE, sizeof(song.scorename));
  if (ret == 0) {
    fprintf(stderr,"uade: no more songs to play\n");
    exit(0);
  } else if (ret < 0) {
    fprintf(stderr, "illegal input (expected score name)\n");
    exit(-1);
  }

  ret = uade_receive_string(song.playername, UADE_COMMAND_PLAYER, sizeof(song.playername));
  if (ret == 0) {
    fprintf(stderr,"expected player name. got nothing.\n");
    exit(-1);
  } else if (ret < 0) {
    fprintf(stderr, "illegal input (expected player name)\n");
    exit(-1);
  }

  ret = uade_receive_message(uc, maxcommand);
  if (ret == 0) {
    fprintf(stderr,"expected module name. got nothing.\n");
    exit(-1);
  } else if (ret < 0) {
    fprintf(stderr, "illegal input (expected module name)\n");
    exit(-1);
  }
  if (uc->msgtype != UADE_COMMAND_MODULE)
    assert(0);
  if (uc->size == 0) {
    song.modulename[0] = 0;
  } else {
    assert(uc->size == (strlen(uc->data) + 1));
    strlcpy(song.modulename, uc->data, sizeof(song.modulename));
  }

  uade_set_automatic_song_end(song.song_end_possible);

  uade_put_long(SCORE_EXEC_DEBUG, uade_execdebugboolean ? 0x12345678 : 0);
  uade_put_long(SCORE_VOLUME_TEST, voltestboolean);
  uade_put_long(SCORE_DMA_WAIT, uade_dmawait);
  uade_put_long(SCORE_MODULECHANGE, disable_modulechange);

  bytesread = uade_safe_load_name(playeraddr, song.playername, "player", uade_highmem - playeraddr);

  if (bytesread > (uade_highmem - playeraddr)) {
    fprintf (stderr, "uade: player %s too big a file (%d bytes)\n", song.playername, bytesread);
    goto skiptonextsong;
  }
  if (bytesread == 0) {
    goto skiptonextsong;
  }

  fprintf(stderr, "uade: player '%s' (%d bytes)\n", song.playername, bytesread);
  /* set player executable address for relocator */
  uade_put_long(SCORE_PLAYER_ADDR, playeraddr);
  len = uade_calc_reloc_size((uae_u32 *) get_real_address(playeraddr),
			     (uae_u32 *) get_real_address(playeraddr + bytesread));
  if (len) {
    relocaddr  = ((playeraddr + bytesread) & 0x7FFFF000) + 0x4000;
    /* + 0x4000 for hippel coso (wasseremu) */
    modaddr = ((relocaddr + len) & 0x7FFFF000) + 0x2000;
  } else {
    fprintf(stderr, "uade: problem with reloc calculation\n");
    goto skiptonextsong;
  }

  if (modaddr <= relocaddr) {
    /* this is very bad because sound core memory allocation will fail */
    fprintf(stderr, "uade: this is very bad: modaddr <= relocaddr: 0x%x <= 0x%x\n", modaddr, relocaddr);
  }

  uade_put_long(SCORE_RELOC_ADDR, relocaddr);  /*address for relocated player*/
  uade_put_long(SCORE_MODULE_ADDR, modaddr);   /* set module address */
  uade_put_long(SCORE_MODULE_LEN, 0);          /* set module size to zero */
  uade_put_long(SCORE_MODULE_NAME_ADDR, 0);    /* mod name address pointer */

  /* load the module if available */
  if (song.modulename[0]) {
    bytesread = uade_safe_load_name(modaddr, song.modulename, "module", uade_highmem - modaddr);
    if (bytesread > (uade_highmem - playeraddr)) {
      fprintf (stderr, "uade: module %s too big a file (%d bytes)\n", song.modulename, bytesread);
      goto skiptonextsong;
    }
    if (bytesread == 0) {
      goto skiptonextsong;
    }
    fprintf(stderr, "uade: module '%s' (%d bytes)\n", song.modulename, bytesread);
    uade_put_long(SCORE_MODULE_LEN, bytesread);

    if (!valid_address(modnameaddr, strlen(song.modulename) + 1)) {
      fprintf(stderr, "uade: invalid address for modulename\n");
      goto skiptonextsong;
    }

    strlcpy(get_real_address(modnameaddr), song.modulename, 1024);
    uade_put_long(SCORE_MODULE_NAME_ADDR, modnameaddr);

  } else {

    if (!valid_address(modnameaddr, strlen(song.playername) + 1)) {
      fprintf(stderr, "uade: invalid address for playername\n");
      goto skiptonextsong;
    }

    strlcpy(get_real_address(modnameaddr), song.playername, 1024);
    uade_put_long(SCORE_MODULE_NAME_ADDR, modnameaddr);

    bytesread = 0;
  }

  uade_player_attribute_check(song.modulename, song.playername, (unsigned char *) get_real_address(modaddr), bytesread);

  /* load sound core (score) */
  if ((file = fopen(song.scorename, "r"))) {
    bytesread = uade_safe_load(scoreaddr, file, uade_highmem - scoreaddr);
    fclose(file);
  } else {
    fprintf (stderr, "uade: can't load score (%s)\n", song.scorename);
    goto skiptonextsong;
  }

  m68k_areg(regs,7) = scoreaddr;
  m68k_setpc(scoreaddr);

  /* override bit for sound format checking */
  uade_put_long(SCORE_FORCE, song.force_by_default);
  /* setsubsong */
  uade_put_long(SCORE_SET_SUBSONG, song.set_subsong);
  uade_put_long(SCORE_SUBSONG, song.subsong);
  /* set ntscbit correctly */
  uade_set_ntsc(song.use_ntsc);

  /* pause bits (don't care!), for debugging purposes only */
  uade_put_long(SCORE_PREPAUSE, 0);
  uade_put_long(SCORE_POSTPAUSE, 0);
  /* set user and supervisor stack pointers */
  uade_put_long(SCORE_USER_STACK, userstack);
  uade_put_long(SCORE_SUPER_STACK, superstack);
  /* no message for score */
  uade_put_long(SCORE_OUTPUT_MSG, 0);
  if ((userstack - (scoreaddr + bytesread)) < 0x1000) {
    fprintf(stderr, "uade: stack over run warning!\n");
  }

  song.set_subsong = 0;

  song.modulename[0] = 0;

  uade_reset_counters();

  flush_sound();

  /* note that uade_speed_hack can be negative (meaning that uade never uses
     speed hack, even if it's requested by the amiga player)! */
  uade_time_critical = 0;
  if (uade_speed_hack > 0) {
    uade_time_critical = 1;
  }

  uade_reboot = 0;

  if (uade_send_message(& (struct uade_msg) {.msgtype = UADE_REPLY_CAN_PLAY, .size = 0}) < 0) {
    fprintf(stderr, "can not send 'CAN_PLAY' reply\n");
    exit(-1);
  }
  return;

 skiptonextsong:
  fprintf(stderr, "uade: skipping to next song\n");
  assert(0);
  goto takenextsong;
}

static void uade_put_long(int addr, int val) {
  uae_u8 *p;
  if (!valid_address(addr, 4)) {
    fprintf(stderr, "uade: invalid uade_put_long (%d)\n", addr);
    return;
  }
  p = get_real_address(addr);
  p[0] = (uae_u8) (val >> 24 & 0xff);
  p[1] = (uae_u8) (val >> 16 & 0xff);
  p[2] = (uae_u8) (val >> 8 & 0xff);
  p[3] = (uae_u8) (val & 0xff);
}

static int uade_get_long(int addr) {
  uae_u8 *ptr;
  int x;
  if (!valid_address(addr, 4)) {
    fprintf(stderr, "uade: invalid uade_get_long (%d)\n", addr);
    return 0;
  }
  ptr = get_real_address(addr);
  x = (ptr[0]) << 24;
  x += (ptr[1]) << 16;
  x += (ptr[2]) << 8;
  x += (int) ptr[3];
  return x;
}

static int uade_safe_load(int dst, FILE *file, int maxlen) {
  const int bufsize = 4096;
  char buf[bufsize];
  int nbytes, len, off;
  len = bufsize;
  off = 0;
  if (maxlen <= 0)
    return 0;
  while (maxlen > 0) {
    if (maxlen < bufsize)
      len = maxlen;
    nbytes = fread(buf, 1, len, file);
    if (!nbytes)
      break;
    if (!valid_address(dst + off, nbytes)) {
      fprintf(stderr, "uade: illegal load range [%x,%x)\n", dst + off, dst + off + nbytes);
      break;
    }
    memcpy(get_real_address(dst + off), buf, nbytes);
    off += nbytes;
    maxlen -= nbytes;
  }
  /* find out how much would have been read even if maxlen was violated */
  while ((nbytes = fread(buf, 1, bufsize, file))) {
    off += nbytes;
  }
  return off;
}

static void uade_safe_get_string(char *dst, int src, int maxlen) {
  int i = 0;
  while(1) {
    if (i >= maxlen) {
      fprintf(stderr, "uade: safe_get_string: i over maxlen\n");
      break;
    }
    if (!valid_address(src + i, 1)) {
      fprintf(stderr, "uade: safe_get_string: invalid memory range\n");
      break;
    }
    dst[i] = *((char *) get_real_address(src + i));
    i++;
  }
  if (maxlen > 0) {
    if (i < maxlen) {
      dst[i] = 0;
    } else { 
      fprintf(stderr, "uade: safe_get_string: null termination warning!\n");
      dst[maxlen-1] = 0;
    }
  }
}

/* if kill_it is zero, uade may switch to next subsong. if kill_it is non-zero
   uade will always switch to next song (if any) */
void uade_song_end(char *reason, int kill_it)
{
  fprintf(stderr, "uade: song end (%s)\n", reason);
  uade_reset_counters();
  assert(0);
  /* slave.song_end(&song, reason, kill_it); */
}


/* check if string is on a safe zone */
static int uade_valid_string(uae_u32 address)
{
  while (valid_address(address, 1)) {
    if (* ((uae_u8 *) get_real_address(address)) == 0)
      return 1;
    address++;
  }
  fprintf(stderr, "uade: invalid string at 0x%x\n", address);
  return 0;
}


void uade_get_amiga_message(void)
{
  uae_u8 *ptr;
  uae_u8 *nameptr;
  FILE *file;
  int x;
  int mins, maxs, curs;
  int status;
  int src, dst, off, len;
  char score_playername[256];   /* playername that eagleplayer reports */
  char score_modulename[256];   /* modulename that eagleplayer reports */
  char score_formatname[256];   /* formatname that eagleplayer reports */

  /* get input message type */
  x = uade_get_long(SCORE_INPUT_MSG);

  switch (x) {
  case AMIGAMSG_SONG_END:
    uade_song_end("player", 0);
    break;

  case AMIGAMSG_SUBSINFO:
    mins = uade_get_long(SCORE_MIN_SUBSONG);
    maxs = uade_get_long(SCORE_MAX_SUBSONG);
    curs = uade_get_long(SCORE_CUR_SUBSONG);
    fprintf(stderr, "uade: subsong info: minimum: %d maximum: %d current: %d\n", mins, maxs, curs);
    break;

  case AMIGAMSG_PLAYERNAME:
    strlcpy(score_playername, get_real_address(0x204), sizeof(score_playername));
    fprintf(stderr,"uade: playername: %s\n", score_playername);
    break;

  case AMIGAMSG_MODULENAME:
    strlcpy(score_modulename, get_real_address(0x204), sizeof(score_modulename));
    fprintf(stderr,"uade: modulename: %s\n", score_modulename);
    break;

  case AMIGAMSG_FORMATNAME:
    strlcpy(score_formatname, get_real_address(0x204), sizeof(score_formatname));
    fprintf(stderr,"uade: formatname: %s\n", score_formatname);
    break;

  case AMIGAMSG_GENERALMSG:
    fprintf(stderr,"uade: general message: %s\n", get_real_address(0x204));
    break;

  case AMIGAMSG_CHECKERROR:
    uade_song_end("module check failed", 1);
    break;

  case AMIGAMSG_SCORECRASH:
    if (uade_debug) {
      fprintf(stderr,"uade: failure: score crashed\n");
      activate_debugger();
      break;
    }
    uade_song_end("score crashed", 1);
    break;

  case AMIGAMSG_SCOREDEAD:
     if (uade_debug) {
      fprintf(stderr,"uade: score is dead\n"); 
      activate_debugger();
      break;
    }
     uade_song_end("score is dead", 1);
    break;

  case AMIGAMSG_LOADFILE:
    /* load a file named at 0x204 (name pointer) to address pointed by
       0x208 and insert the length to 0x20C */
    src = uade_get_long(0x204);
    if (!uade_valid_string(src)) {
      fprintf(stderr, "uade: load: name in invalid address range\n");
      break;
    }
    nameptr = get_real_address(src);
    if ((file = uade_open_amiga_file(nameptr))) {
      dst = uade_get_long(0x208);
      len = uade_safe_load(dst, file, uade_highmem - dst);
      fclose(file); file = 0;
      uade_put_long(0x20C, len);
      /* fprintf(stderr, "uade: load: %s: ptr = 0x%x size = 0x%x\n", nameptr, dst, len); */
    }
    break;

  case AMIGAMSG_READ:
    src = uade_get_long(0x204);
    if (!uade_valid_string(src)) {
      fprintf(stderr, "uade: read: name in invalid address range\n");
      break;
    }
    nameptr = get_real_address(src);
    dst = uade_get_long(0x208);
    off = uade_get_long(0x20C);
    len = uade_get_long(0x210);
    /* fprintf(stderr,"uade: read: '%s' dst = 0x%x off = 0x%x len = 0x%x\n", nameptr, dst, off, len); */
    if ((file = uade_open_amiga_file(nameptr))) {
      if (fseek(file, off, SEEK_SET)) {
	perror("can not fseek to position");
	x = 0;
      } else {
	x = uade_safe_load(dst, file, len);
	if (x > len)
	  x = len;
      }
      fclose(file); file = 0;
      uade_put_long(0x214, x);
    } else {
      fprintf(stderr, "uade: read: error when reading '%s'\n", nameptr);
      uade_put_long(0x214, 0);
    }
    break;

  case AMIGAMSG_FILESIZE:
    src = uade_get_long(0x204);
    if (!uade_valid_string(src)) {
      fprintf(stderr, "uade: filesize: name in invalid address range\n");
      break;
    }
    nameptr = get_real_address(src);
    if ((file = uade_open_amiga_file(nameptr))) {
      fseek(file, 0, SEEK_END);
      len = ftell(file);
      fclose(file); file = 0;
      /* fprintf(stderr, "uade: size: 0x%x '%s'\n", len, nameptr); */
      uade_put_long(0x208, len);
      uade_put_long(0x20C, -1);
    } else {
      fprintf(stderr, "uade: can't get file size for '%s'\n", nameptr);
      uade_put_long(0x208, 0);
      uade_put_long(0x20C, 0);
    }
    break;

  case AMIGAMSG_TIME_CRITICAL:
    uade_time_critical = uade_get_long(0x204) ? 1 : 0;
    if (uade_speed_hack < 0) {
      /* a negative value forbids use of speed hack */
      uade_time_critical = 0;
    }
    break;

  case AMIGAMSG_GET_INFO:
    src = uade_get_long(0x204);
    dst = uade_get_long(0x208);
    len = uade_get_long(0x20C);
    if (!uade_valid_string(src) || !uade_valid_string(dst)) {
      fprintf(stderr, "uade: invalid address from 0x%x or 0x%x\n", src, dst);
      break;
    }
    len = uade_get_info(get_real_address(dst), get_real_address(src), len);
    uade_put_long(0x20C, len);
    break;

  default:
    fprintf(stderr,"uade: unknown message from score ($%x)\n",x);
    break;
  }
}


void uade_change_subsong(int subsong) {
  song.cur_subsong = subsong;
  fprintf(stderr, "uade: current subsong %d\n", subsong);
  uade_put_long(SCORE_SUBSONG, subsong);
  uade_send_amiga_message(AMIGAMSG_SETSUBSONG);
  flush_sound();

}

void uade_set_ntsc(int usentsc) {
  uade_put_long(SCORE_NTSC, usentsc);
}

void uade_set_automatic_song_end(int song_end_possible) {
  uade_put_long(SCORE_HAVE_SONGEND, song_end_possible);
}

void uade_send_amiga_message(int msgtype) {
  uade_put_long(SCORE_OUTPUT_MSG, msgtype);
}


void uade_reset_counters(void) {

  uade_zero_sample_count = 0;    /* only useful in non-slavemode */

  uade_vsync_counter = 0;
}


void uade_swap_buffer_bytes(void *data, int bytes)
{
  uae_u8 *buf = (uae_u8 *) data;
  uae_u8 sample;
  int i;
  assert((bytes % 2) == 0);
  for (i = 0; i < bytes; i += 2) {
    sample = buf[i + 0];
    buf[i + 0] = buf[i + 1];
    buf[i + 1] = sample;
  }
}


void uade_test_sound_block(void *buf, int size) {
  int i, s, exceptioncounter, bytes;

  if (song.silence_timeout <= 0)
    return;

  if (currprefs.sound_bits == 16) {

    uae_s16 *sm = (uae_s16 *) buf;
    exceptioncounter = 0;

    for (i=0; i < (size/2); i++) {
      s = sm[i] >= 0 ? sm[i] : -sm[i];
      if (s >= (32767*1/100)) {
	exceptioncounter++;
	if (exceptioncounter > (size/2/100)) {
	  uade_zero_sample_count = 0;
	  break;
	}
      }
    }
    if (i == (size/2)) {
      uade_zero_sample_count += size;
    }

  } else {

    uae_s8 *sm = (uae_s8 *) buf;
    exceptioncounter = 0;

    for (i=0; i < size; i++) {
      s = sm[i] >= 0 ? sm[i] : -sm[i];
      if (s >= (127*2/100)) {
	exceptioncounter++;
	if (exceptioncounter > (size/100)) {
	  uade_zero_sample_count = 0;
	  break;
	}
      }
    }
    if (i == size) {
      uade_zero_sample_count += size;
    }
  }

  bytes = song.silence_timeout * currprefs.sound_bits/8 * currprefs.sound_freq;
  if (currprefs.stereo) {
    bytes *= 2;
  }
  if (uade_zero_sample_count >= bytes) {
    char reason[256];
    sprintf(reason, "silence detected (%d seconds)", song.silence_timeout);
    uade_song_end(reason, 0);
  }
}

void uade_vsync_handler(void)
{
  uade_vsync_counter++;

  if (song.timeout >= 0) {
    if ((uade_vsync_counter/50) >= song.timeout) {
      char reason[256];
      sprintf(reason, "timeout %d seconds", song.timeout);
      /* don't skip to next subsong even if available (kill == 1) */
      uade_song_end(reason, 1);
      return;
    }
  }

  if (song.subsong_timeout >= 0) {
    if ((uade_vsync_counter/50) >= song.subsong_timeout) {
      char reason[256];
      sprintf(reason, "per subsong timeout %d seconds", song.subsong_timeout);
      /* skip to next subsong if available (kill == 0) */
      uade_song_end(reason, 0);
      return;
    }
  }
}
