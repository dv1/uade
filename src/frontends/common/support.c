#include <stdio.h>

#include "support.h"

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
