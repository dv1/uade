#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "support.h"
#include "strlrep.h"


#define LINESIZE 1024


/* Split line with respect to white space. */
char **uade_split_line(size_t *nitems, size_t *lineno, FILE *f,
		       const char *delimiters)
{
	char line[LINESIZE], templine[LINESIZE];
	char **items = NULL;
	size_t pos;
	char *sp, *s;

	*nitems = 0;

	while (xfgets(line, sizeof line, f) != NULL) {

		if (lineno != NULL)
			(*lineno)++;

		/* Skip, if a comment line */
		if (line[0] == '#')
			continue;

		/* strsep() modifies line that it touches, so we make a copy
		   of it, and then count the number of items on the line */
		strlcpy(templine, line, sizeof(templine));
		sp = templine;
		while ((s = strsep(&sp, delimiters)) != NULL) {
			if (*s == 0)
				continue;
			(*nitems)++;
		}

		if (*nitems > 0)
			break;
	}

	if (*nitems == 0)
		return NULL;

	if ((items = malloc(sizeof(items[0]) * (*nitems + 1))) == NULL)
		uadeerror("No memory for nws items.\n");

	sp = line;
	pos = 0;
	while ((s = strsep(&sp, delimiters)) != NULL) {
		if (*s == 0)
			continue;

		if ((items[pos] = strdup(s)) == NULL)
			uadeerror("No memory for an nws item.\n");

		pos++;
	}
	items[pos] = NULL;
	assert(pos == *nitems);

	return items;
}


char *xfgets(char *s, int size, FILE *stream)
{
	char *ret;

	while (1) {
		ret = fgets(s, size, stream);
		if (ret != NULL)
			break;

		if (feof(stream) || ferror(stream))
			break;
	}

	return ret;
}
