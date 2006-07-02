#ifndef _UADE_SONGDB_H_
#define _UADE_SONGDB_H_

#include "eagleplayer.h"

int uade_add_playtime(const char *md5, uint32_t playtime, int replaceandsort);
void uade_analyze_song_from_songdb(struct uade_song *us);
int uade_find_playtime(const char *md5);
void uade_md5_from_buffer(char *dest, size_t destlen,
			  uint8_t *buf, size_t bufsize);
int uade_read_content_db(const char *filename);
int uade_read_song_conf(const char *filename);
void uade_save_content_db(const char *filename);
int uade_update_song_conf(const char *songconfin, const char *songconfout,
			  const char *songname, const char *options);

#endif
