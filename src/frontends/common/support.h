#ifndef _UADE_SUPPORT_H_
#define _UADE_SUPPORT_H_

#define uadeerror(fmt, args...) do { fprintf(stderr, "uade: " fmt, ## args); exit(1); } while (0)

/* Same as fgets(), but guarantees that feof() or ferror() have happened
   when xfgets() returns NULL */
char *xfgets(char *s, int size, FILE *stream);

#endif
