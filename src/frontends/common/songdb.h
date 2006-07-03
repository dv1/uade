#ifndef _UADE_SONGDB_H_
#define _UADE_SONGDB_H_

#include "eagleplayer.h"
#include "vplist.h"


struct uade_content {
  char md5[33];
  uint32_t playtime; /* in milliseconds */
  struct vplist *subs;
};


struct uade_content *uade_add_playtime(const char *md5, uint32_t playtime,
				       int replaceandsort);
struct uade_song *uade_alloc_song(const char *filename);
void uade_analyze_song_from_songdb(struct uade_song *us);
void uade_md5_from_buffer(char *dest, size_t destlen,
			  uint8_t *buf, size_t bufsize);
int uade_read_content_db(const char *filename);
int uade_read_song_conf(const char *filename);
void uade_save_content_db(const char *filename);
void uade_unalloc_song(struct uade_song *us);
int uade_update_song_conf(const char *songconfin, const char *songconfout,
			  const char *songname, const char *options);

#endif
