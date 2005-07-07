/* UADE - Unix Amiga Delitracker Emulator
 * Copyright 2000-2004, Heikki Orsila <heikki.orsila@iki.fi>
 * See http://uade.ton.tut.fi
 */

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

#include "uade.h"
#include "../config.h"

#include "strlrep.h"

#include "players.h"


static int uade_calc_reloc_size(uae_u32 *src, uae_u32 *end);
static void uade_flush_sound(void);
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

static int uade_highmem = 0x200000;

struct uade_slave slave;

struct uade_song uade_song;

static int uade_dmawait = 0;

static int uade_execdebugboolean = 0;
static int voltestboolean = 0;
static int disable_modulechange = 0;

/* This should be non-zero if native sound output shouldn't be used.
   This is set to non-zero in slavemode and outpipemode              */
int uade_local_sound = 1;
char *uade_unix_sound_device = 0;

int uade_using_outpipe = 0;

int uade_reboot;

int uade_debug = 0;

static int uade_do_panning = 0;
static float uade_pan_value = 1.0f;

int uade_swap_output_bytes = 0;

/* contains uade's command line name */
static char *uadecmdlinename = 0;

static int uade_vsync_counter;

static int uade_zero_sample_count;

static int uade_speed_hack = 0;
int uade_time_critical = 0;


void uade_option(int argc, char **argv)
{
  int i, j, no_more_opts;
  int xmms_slave = 0;
  int shell_interaction = 0;
  void (*slave_functions)(struct uade_slave *) = 0;
  char **s_argv;
  int s_argc;
  int cfg_loaded = 0;
  char optionsfile[UADE_PATH_MAX];

  uadecmdlinename = strdup(argv[0]);

  uade_song.playername[0] = 0;
  uade_song.modulename[0] = 0;
  uade_song.song_end_possible = 1;

  no_more_opts = 0;

  s_argv = malloc(sizeof(char *) * (argc + 1));
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

      if (!strcmp(argv[i], "--xmms-slave")) {
	xmms_slave = 1;
	/* '--xmms-slave' and its argument, the map file name, should be
	   passed on to the xmms slave code so we don't increment 'i'
	   here */

      } else if (!strncmp (argv[i], "-config=", 8)) {
	strlcpy(optionsfile, argv[i] + 8, sizeof(optionsfile));
	fprintf(stderr,"uade: config '%s'\n", optionsfile);
	if (cfgfile_load (&currprefs, optionsfile)) {
	  cfg_loaded = 1;
	} else {
	  fprintf(stderr, "uade: couldn't load uaerc (%s)!\n", optionsfile);
	  exit(-1);
	}
	i++;

      } else if (!strcmp(argv[i], "-force") || !strcmp(argv[i], "-f")) {
	uade_song.force_by_default = 1;
	i++;

      } else if (!strcmp(argv[i], "-execdebug")) {
	fprintf(stderr, "-execdebug parameter\n");
	uade_execdebugboolean = 1;
	i++;

      } else if (!strcmp(argv[i], "-voltest")) {
	fprintf(stderr, "-voltest parameter\n");
	voltestboolean = 0x12345678;
	i++;

      } else if (!strcmp(argv[i], "-dmawait")) {
	if ((i+1) >= argc) {
	  fprintf(stderr, "parameter missing for -dmawait\n");
	  uade_print_help(1);
	  exit(-1);
	}
	uade_dmawait = atoi(argv[i + 1]);
	fprintf(stderr, "setting dmawait = %d rasterlines\n", uade_dmawait);
	uade_dmawait += 0x12340000;
	i += 2;

      } else if (!strcmp(argv[i], "-ntsc")) {
	uade_song.use_ntsc = 1;
	i++;
	
      } else if (!strcmp(argv[i], "-no-end") || !strcmp(argv[i], "-ne")) {
	uade_song.song_end_possible = 0;
	i++;

      } else if (!strcmp(argv[i], "-pan") || !strcmp(argv[i], "-p")) {
	if ((i+1) >= argc) {
	  fprintf(stderr, "%s parameter missing\n", argv[i]);
	  uade_print_help(1);
	  exit(-1);
	}
	uade_do_panning = 1;
	uade_pan_value = atof(argv[i+1]);
	if (uade_pan_value < 0.0f || uade_pan_value > 2.0f) {
	  fprintf(stderr, "%s parameter is illegal. Use proper range [0, 1]. See help.\n", argv[i]);
	  uade_print_help(1);
	  exit(-1);
	}
	i += 2;

      } else if (strcmp(argv[i], "-swap-bytes") == 0) {
	uade_swap_output_bytes = 1;
	i++;

      } else if (strcmp(argv[i], "-no-mc") == 0) {
	disable_modulechange = 1;
	i++;
	
      } else if (!strcmp(argv[i], "-debug") || !strcmp(argv[i], "-d")) {
	uade_debug = 1;
	i++;
	
      } else if (!strcmp(argv[i], "-sh")) {
	uade_speed_hack = 1;
	i++;
	
      } else if (!strcmp(argv[i], "-no-sh")) {
	uade_speed_hack = -1;
	i++;
	
	/* -fi, -fil, ..., -filter turn on filter emulation */
      } else if (!strncmp(argv[i], "-filter", 3)) {
	uade_song.use_filter = 1;
	i++;

      } else if (!strcmp(argv[i], "-fs")) {
	if ((i + 1) >= argc) {
	  fprintf(stderr, "%s parameter missing\n", argv[i]);
	  uade_print_help(1);
	  exit(-1);
	}
	uade_song.use_filter = 1;
	gui_ledstate_forced = atoi(argv[i + 1]) ? 1 : 2;
	gui_ledstate = gui_ledstate_forced & 1 ? 1 : 0;
	i += 2;

      } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") || !strcmp(argv[i], "-help")) {
	uade_print_help(0);
	exit(0);

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

  if (!cfg_loaded) {
    strlcpy(optionsfile, "../uaerc", sizeof(optionsfile));
    if (!cfgfile_load(&currprefs, optionsfile)) {
      fprintf(stderr, "uade: couldn't load uaerc (%s)!\n", optionsfile);
      exit(-1);
    }
  }

  uade_get_path(uade_song.scorename, UADE_PATH_SCORE, sizeof(uade_song.scorename));

  memset(&slave, 0, sizeof(slave));

#ifdef HAVE_UNIX_SHELL
  slave_functions = us_functions;
#endif

#ifdef HAVE_XMMS_SLAVE
  if (xmms_slave)
    slave_functions = xmms_slave_functions;
#endif

#ifdef HAVE_UNIX_SHELL_INT
  if (shell_interaction)
    slave_functions = usi_functions;
#endif

#ifdef HAVE_AMIGA_SHELL
  slave_functions = as_functions;
#endif

  slave_functions(&slave);

  slave.timeout = -1;		/* default timeout infinite */
  slave.subsong_timeout = -1;	/* default per subsong timeout infinite */
  slave.silence_timeout = -1;	/* default silence timeout infinite */

  slave.setup(&uade_song, s_argc, s_argv);

  free(s_argv);

  if (uade_debug)
    activate_debugger();

  uade_reboot = 1;
}


void uade_print_help(int problemcode) {
  switch (problemcode) {
  case 0:
    /* just for printing help */
    break;
  case 1:
    fprintf(stderr, "Illegal parameters for UADE\n\n");
    break;
  case 2:
    fprintf(stderr, "No songs given as parameters for UADE\n\n");
    break;
  default:
    fprintf(stderr, "Unknown error\n");
    break;
  }
  fprintf(stderr, "UADE usage:\n");
  fprintf(stderr, " %s [OPTIONS] [FILE1] [FILE2] ...\n\n", uadecmdlinename);

  fprintf(stderr, " mode options:\n");
  fprintf(stderr, " -i\t\tSet command line interactive mode\n");
  fprintf(stderr, "\n");

  fprintf(stderr, " general options:\n");
  fprintf(stderr, " -force/-f\tForces eagleplayer to play a given song if it's not recognized\n");
  fprintf(stderr, " -pan/-p value\tSets panning value in the range of [0.00, 1.00] (mix channels)\n");
  fprintf(stderr, " -filter/-fi\tEnable filter emulation\n");
  fprintf(stderr, " -fs x\t\tForce filter state to be x. 0 is off. 1 is on.\n");
  fprintf(stderr, " -no-end/-ne\tThe first song will be played indefinetely (probably)\n");
  fprintf(stderr, " -ntsc\t\tForce Amiga into NTSC mode (may affect playback speed)\n");
  fprintf(stderr, "\n");

  fprintf(stderr, " unix shell back-end options:\n");
  fprintf(stderr, " -sub/-s numb\tSets first subsong number to be played\n");
  fprintf(stderr, " -repeat/-rp\tLoop song list to be played from beginning after the end\n");
  fprintf(stderr, " -one\t\tPlay at most one subsong / file\n");

  fprintf(stderr, " -outpipe x\tDump all sound output to file descriptor x (x=1 => stdout)\n");
  fprintf(stderr, " -score/-S file\tUse 'file' as the sound core (experts only)\n");
  fprintf(stderr, " -swap-bytes\tSwaps output sample bytes (use only with 16-bit samples)\n");
  fprintf(stderr, " -t timeout\tSets the maximum number of seconds to play a (sub)song\n");
  fprintf(stderr, " -st timeout\tSwitch to next subsong after 'timeout' seconds\n");
  fprintf(stderr, " -sit timeout\tSwitch to next subsong after 'timeout' seconds silent\n");
  fprintf(stderr, " -device\tSelect sound device filename (UNIX)\n");
  fprintf(stderr, " -rand/-r\tPlay songs in random order\n");
  fprintf(stderr, " -recursive/-R\tAdd directories recursively to playlist\n");
  fprintf(stderr, " -mod/-M file\tSets songname to be played\n");
  fprintf(stderr, " -pl/-P file\tSets eagleplayer to be used (this can be used for custom songs)\n");
  fprintf(stderr, "\n");

  fprintf(stderr, "examples:\n");
  fprintf(stderr, "\tplay some files with panning:\n\t\tuade -p 0.7 cust.*\n");
  fprintf(stderr, "\tplay a directory hierarchy in random order:\n\t\tuade -r -R /chip/directory\n");
  fprintf(stderr, "\tplay each (sub)song at most 256 seconds:\n\t\tuade -t 256 fc13.Defjam-CCS-ACC1\n");
}


static int uade_safe_load_name(int vaddr, char *name, const char *expl,
			       int maxlen)
{
  int bytesread, status;
  FILE *file;
  file = fopen(name, "rb");
  if (!file) {
    fprintf(stderr,"uade: couldn't load %s %s\n", expl, name);
    return 0;
  }
  if ((status = decrunch (&file, name)) < 0) {
    fprintf (stderr, "uade: decrunching error on %s %s", expl, name);
    fclose(file);
    return 0;
  }
  bytesread = uade_safe_load(vaddr, file, maxlen);
  fclose(file);
  return bytesread;
}

/* this is called for each played song from newcpu.c/m68k_reset() */
void uade_prerun(void) {
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

  /* IMPORTANT:
     It seems that certain players don't work totally reliably if memory
     contains trash from previous songs. To be certain that each song is
     played from the same initial state of emulator we clear the memory
     from 0x400 to 0x100000 each time a new song is played */
  uade_highmem = 0;
  while (uade_highmem < 0x200000) {
    if (!valid_address(0, uade_highmem + 0x10000))
      break;
    uade_highmem += 0x10000;
  }
  if (uade_highmem < 0x80000) {
    fprintf(stderr, "uade: fatal error: highmem == 0x%x (< 0x80000)!\n", uade_highmem);
    exit(-1);
  }
  if (uade_highmem < 0x200000) {
    fprintf(stderr, "uade: warning: highmem == 0x%x (< 0x200000)!\n", uade_highmem);
  }
  memset(get_real_address(0), 0, uade_highmem);
  
 takenextsong:
  
  /* if no more songs in the play queue, quit cmdline tool */
  if (!slave.get_next(&uade_song)) {
    fprintf(stderr,"uade: no more songs to play\n");
    exit(0);
  }

  uade_set_automatic_song_end(uade_song.song_end_possible);

  uade_put_long(SCORE_EXEC_DEBUG, uade_execdebugboolean ? 0x12345678 : 0);
  uade_put_long(SCORE_VOLUME_TEST, voltestboolean);
  uade_put_long(SCORE_DMA_WAIT, uade_dmawait);
  uade_put_long(SCORE_MODULECHANGE, disable_modulechange);

  bytesread = uade_safe_load_name(playeraddr, uade_song.playername, "player", uade_highmem - playeraddr);

  if (bytesread > (uade_highmem - playeraddr)) {
    fprintf (stderr, "uade: player %s too big a file (%d bytes)\n", uade_song.playername, bytesread);
    goto skiptonextsong;
  }
  if (bytesread == 0) {
    goto skiptonextsong;
  }

  fprintf(stderr, "uade: player '%s' (%d bytes)\n", uade_song.playername, bytesread);
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
  if (uade_song.modulename[0]) {
    bytesread = uade_safe_load_name(modaddr, uade_song.modulename, "module", uade_highmem - modaddr);
    if (bytesread > (uade_highmem - playeraddr)) {
      fprintf (stderr, "uade: module %s too big a file (%d bytes)\n", uade_song.modulename, bytesread);
      goto skiptonextsong;
    }
    if (bytesread == 0) {
      goto skiptonextsong;
    }
    fprintf(stderr, "uade: module '%s' (%d bytes)\n", uade_song.modulename, bytesread);
    uade_put_long(SCORE_MODULE_LEN, bytesread);

    if (!valid_address(modnameaddr, strlen(uade_song.modulename) + 1)) {
      fprintf(stderr, "uade: invalid address for modulename\n");
      goto skiptonextsong;
    }

    strlcpy(get_real_address(modnameaddr), uade_song.modulename, 1024);
    uade_put_long(SCORE_MODULE_NAME_ADDR, modnameaddr);

  } else {

    if (!valid_address(modnameaddr, strlen(uade_song.playername) + 1)) {
      fprintf(stderr, "uade: invalid address for playername\n");
      goto skiptonextsong;
    }

    strlcpy(get_real_address(modnameaddr), uade_song.playername, 1024);
    uade_put_long(SCORE_MODULE_NAME_ADDR, modnameaddr);

    bytesread = 0;
  }

  uade_player_attribute_check(uade_song.modulename, uade_song.playername, (unsigned char *) get_real_address(modaddr), bytesread);

  /* load sound core (score) */
  if ((file = fopen(uade_song.scorename, "rb"))) {
    bytesread = uade_safe_load(scoreaddr, file, uade_highmem - scoreaddr);
    fclose(file);
  } else {
    fprintf (stderr, "uade: can't load score (%s)\n", uade_song.scorename);
    goto skiptonextsong;
  }

  m68k_setpc(scoreaddr);
  /* override bit for sound format checking */
  uade_put_long(SCORE_FORCE, uade_song.force_by_default);
  /* setsubsong */
  uade_put_long(SCORE_SET_SUBSONG, uade_song.set_subsong);
  uade_put_long(SCORE_SUBSONG, uade_song.subsong);
  /* set ntscbit correctly */
  uade_set_ntsc(uade_song.use_ntsc);
  /* set filter emulation */
  sound_use_filter = uade_song.use_filter;
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

  uade_song.set_subsong = 0;

  uade_song.modulename[0] = 0;

  if (slave.post_init)
    slave.post_init();

  uade_reset_counters();

  uade_flush_sound();

  /* note that uade_speed_hack can be negative (meaning that uade never uses
     speed hack, even if it's requested by the amiga player)! */
  uade_time_critical = 0;
  if (uade_speed_hack > 0) {
    uade_time_critical = 1;
  }

  uade_reboot = 0;
  return;

 skiptonextsong:
  fprintf(stderr, "uade: skipping to next song\n");
  slave.skip_to_next_song();
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
  slave.song_end(&uade_song, reason, kill_it);
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
  case UADE_SONG_END:
    uade_song_end("player", 0);
    break;
  case UADE_SUBSINFO:
    mins = uade_get_long(SCORE_MIN_SUBSONG);
    maxs = uade_get_long(SCORE_MAX_SUBSONG);
    curs = uade_get_long(SCORE_CUR_SUBSONG);
    if (slave.subsinfo) {
      slave.subsinfo(&uade_song, mins, maxs, curs);
    } else {
      fprintf(stderr, "uade: subsong info: minimum: %d maximum: %d current: %d\n", mins, maxs, curs);
    }
    break;
  case UADE_PLAYERNAME:
    strlcpy(score_playername, get_real_address(0x204), sizeof(score_playername));
    if (slave.got_playername) {
      slave.got_playername(score_playername);
    } else {
      fprintf(stderr,"uade: playername: %s\n", score_playername);
    }
    break;
  case UADE_MODULENAME:
    strlcpy(score_modulename, get_real_address(0x204), sizeof(score_modulename));
    if (slave.got_modulename) {
      slave.got_modulename(score_modulename);
    } else {
      fprintf(stderr,"uade: modulename: %s\n", score_modulename);
    }
    break;
  case UADE_FORMATNAME:
    strlcpy(score_formatname, get_real_address(0x204), sizeof(score_formatname));
    if (slave.got_formatname) {
      slave.got_formatname(score_formatname);
    } else {
      fprintf(stderr,"uade: formatname: %s\n", score_formatname);
    }
    break;
  case UADE_GENERALMSG:
    fprintf(stderr,"uade: general message: %s\n", get_real_address(0x204));
    break;
  case UADE_CHECKERROR:
    uade_song_end("module check failed", 1);
    break;
  case UADE_SCORECRASH:
    if (uade_debug) {
      fprintf(stderr,"uade: failure: score crashed\n");
      activate_debugger();
      break;
    }
    uade_song_end("score crashed", 1);
    break;
  case UADE_SCOREDEAD:
     if (uade_debug) {
      fprintf(stderr,"uade: score is dead\n"); 
      activate_debugger();
      break;
    }
     uade_song_end("score is dead", 1);
    break;
  case UADE_LOADFILE:
    /* load a file named at 0x204 (name pointer) to address pointed by
       0x208 and insert the length to 0x20C */
    src = uade_get_long(0x204);
    if (!uade_valid_string(src)) {
      fprintf(stderr, "uade: load: name in invalid address range\n");
      break;
    }
    nameptr = get_real_address(src);
    if ((file = uade_open_amiga_file(nameptr))) {
      if ((status = decrunch (&file, nameptr)) < 0) {
	fprintf (stderr, "uade: load: decrunching error\n");
	fclose(file);
	break;
      } else {
	dst = uade_get_long(0x208);
	len = uade_safe_load(dst, file, uade_highmem - dst);
	fclose(file); file = 0;
	uade_put_long(0x20C, len);
	/* fprintf(stderr, "uade: load: %s: ptr = 0x%x size = 0x%x\n", nameptr, dst, len); */
      }
    }
    break;
  case UADE_READ:
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
  case UADE_FILESIZE:
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

  case UADE_TIME_CRITICAL:
    uade_time_critical = uade_get_long(0x204) ? 1 : 0;
    if (uade_speed_hack < 0) {
      /* a negative value forbids use of speed hack */
      uade_time_critical = 0;
    }
    break;

  case UADE_GET_INFO:
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
  uade_song.cur_subsong = subsong;
  fprintf(stderr, "uade: current subsong %d\n", subsong);
  uade_put_long(SCORE_SUBSONG, subsong);
  uade_send_amiga_message(UADE_SETSUBSONG);
  uade_flush_sound();

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

static void uade_flush_sound(void) {
  if (slave.flush_sound)
    slave.flush_sound();
  flush_sound();
}


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


void uade_swap_buffer_bytes(void *data, int bytes) {
  uae_u8 *buf = (uae_u8 *) data;
  uae_u8 sample;
  int i;
  /* even number bytes */
  if ((bytes % 2) == 1)
    bytes--;
  for(i = 0; i < bytes; i += 2) {
    sample = buf[i + 0];
    buf[i + 0] = buf[i + 1];
    buf[i + 1] = sample;
  }
}

void uade_reset_counters(void) {

  uade_zero_sample_count = 0;    /* only useful in non-slavemode */

  uade_vsync_counter = 0;
  uade_audxdat_counter = 0;
  uade_audxvol_counter = 0;
  uade_audxlch_counter = 0;
}

void uade_test_sound_block(void *buf, int size) {
  int i, s, exceptioncounter, bytes;

  if (slave.silence_timeout <= 0)
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

  bytes = slave.silence_timeout * currprefs.sound_bits/8 * currprefs.sound_freq;
  if (currprefs.stereo) {
    bytes *= 2;
  }
  if (uade_zero_sample_count >= bytes) {
    char reason[256];
    sprintf(reason, "silence detected (%d seconds)", slave.silence_timeout);
    uade_song_end(reason, 0);
  }
}

void uade_vsync_handler(void)
{
  uade_vsync_counter++;

  if (slave.timeout >= 0) {
    if ((uade_vsync_counter/50) >= slave.timeout) {
      char reason[256];
      sprintf(reason, "timeout %d seconds", slave.timeout);
      /* don't skip to next subsong even if available (kill == 1) */
      uade_song_end(reason, 1);
      return;
    }
  }

  if (slave.subsong_timeout >= 0) {
    if ((uade_vsync_counter/50) >= slave.subsong_timeout) {
      char reason[256];
      sprintf(reason, "per subsong timeout %d seconds", slave.subsong_timeout);
      /* skip to next subsong if available (kill == 0) */
      uade_song_end(reason, 0);
      return;
    }
  }
}


/* last part of the audio system pipeline. returns non-zero if sndbuffer
   should be written to os audio drivers (such as oss or alsa), otherwise
   returns zero. the reason why sndbuffer should not be written to os audio
   drivers could be that the user has issued -outpipe or is using some
   slave audio target.
*/
int uade_check_sound_buffers(void *sndbuffer, int sndbufsize, int bytes_per_sample)
{
  /* effects */
  if (uade_do_panning) {
    if (currprefs.stereo) {
      int to_frames_divisor = bytes_per_sample * 2;
      uade_effect_pan((short *) sndbuffer, sndbufsize / to_frames_divisor, bytes_per_sample, uade_pan_value);
    }
  }

  /* silence testing */
  uade_test_sound_block((void *) sndbuffer, sndbufsize);

  /* endianess tricks */
  if (uade_swap_output_bytes)
    uade_swap_buffer_bytes(sndbuffer, sndbufsize);

  if (slave.write) {
    slave.write(sndbuffer, sndbufsize);

  } else if (uade_using_outpipe) {
    uade_write_to_outpipe(sndbuffer, sndbufsize);

  } else {
    /* write sndbuffer to os audio driver */
    return 1;
  }

  /* do not write sndbuffer to os audio driver */
  return 0;
}
