/* 
 * UADE
 * 
 * Support for ALSA sound
 * 
 * Copyright 2004 Heikki Orsila <heikki.orsila@iki.fi>
 */

#include <ao/ao.h>
#include <errno.h>
#include <string.h>

#include "uade.h"
#include "uade-os.h"

extern uae_u16 sndbuffer[];
extern uae_u16 *sndbufpt;
extern int sndbufsize;
extern int sound_bytes_per_sample;
extern ao_device *libao_device;

extern void finish_sound_buffer (void);


static void check_sound_buffers (void) {
  if ((char *) sndbufpt - (char *) sndbuffer >= sndbufsize) {
    
    if ((char *) sndbufpt - (char *) sndbuffer > sndbufsize) {
      fprintf(stderr, "uade: A bug in sound buffer writing. Report this!\n");
    }
    
    if (uade_check_sound_buffers(sndbuffer, sndbufsize, sound_bytes_per_sample)) {
      if (!ao_play(libao_device, (char *) sndbuffer, sndbufsize)) {
	fprintf(stderr, "uade: libao error detected. exit.\n");
	uade_exit(-1);
      }
    }
    
    sndbufpt = sndbuffer;
  }
}

#define PUT_SOUND_BYTE(b) do { *(uae_u8 *)sndbufpt = b; sndbufpt = (uae_u16 *)(((uae_u8 *)sndbufpt) + 1); } while (0)
#define PUT_SOUND_WORD(b) do { *(uae_u16 *)sndbufpt = b; sndbufpt = (uae_u16 *)(((uae_u8 *)sndbufpt) + 2); } while (0)
#define PUT_SOUND_BYTE_LEFT(b) PUT_SOUND_BYTE(b)
#define PUT_SOUND_WORD_LEFT(b) PUT_SOUND_WORD(b)
#define PUT_SOUND_BYTE_RIGHT(b) PUT_SOUND_BYTE(b)
#define PUT_SOUND_WORD_RIGHT(b) PUT_SOUND_WORD(b)
#define SOUND16_BASE_VAL 0
#define SOUND8_BASE_VAL 128

#define DEFAULT_SOUND_MAXB 8192
#define DEFAULT_SOUND_MINB 8192
#define DEFAULT_SOUND_BITS 16
#define DEFAULT_SOUND_FREQ 44100
#define HAVE_STEREO_SUPPORT
