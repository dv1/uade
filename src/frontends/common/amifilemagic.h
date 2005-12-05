#ifndef _UADE_AMIFILEMAGIC_H_
#define _UADE_AMIFILEMAGIC_H_

#include <stdio.h>

void uade_filemagic(unsigned char *buf, char *pre, size_t realfilesize, size_t bufsize, const char *filename);

#endif
