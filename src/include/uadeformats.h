#ifndef _UADE_UADEFORMATS_H_
#define _UADE_UADEFORMATS_H_

char *uade_get_playername(const char *extension, void *formats, int nformats);
void *uade_read_uadeformats(int *nformats, char *filename);

#endif
