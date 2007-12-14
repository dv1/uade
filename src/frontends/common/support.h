#ifndef _UADE_SUPPORT_H_
#define _UADE_SUPPORT_H_

#include <stdio.h>

#define uadeerror(fmt, args...) do { fprintf(stderr, "uade: " fmt, ## args); exit(1); } while (0)

#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* Same as fgets(), but guarantees that feof() or ferror() have happened
   when xfgets() returns NULL */
char *xfgets(char *s, int size, FILE *stream);

char **uade_split_line(size_t *nitems, size_t *lineno, FILE *f,
		       const char *delimiters);

#endif
