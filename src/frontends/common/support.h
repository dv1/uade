#ifndef _UADE_SUPPORT_H_
#define _UADE_SUPPORT_H_

/* Same as fgets(), but guarantees that feof() or ferror() have happened
   when xfgets() returns NULL */
char *xfgets(char *s, int size, FILE *stream);

#endif
