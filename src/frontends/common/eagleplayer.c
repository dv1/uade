/* UADE
 *
 * Copyright 2005-2007 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * Loads contents of 'eagleplayer.conf' and 'song.conf'. The file formats are
 * specified in doc/uade123.1.
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) in any you
 * want in your projects.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>

#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "eagleplayer.h"
#include "strlrep.h"
#include "amifilemagic.h"
#include "uadeconf.h"
#include "unixatomic.h"
#include "songdb.h"
#include "support.h"

#define LINESIZE (1024)
#define OPTION_DELIMITER ","

#define eperror(fmt, args...) do { fprintf(stderr, "Eagleplayer.conf error on line %zd: " fmt "\n", lineno, ## args); exit(-1); } while (0)

struct attrlist {
	char *s;
	int e;
	enum uade_attribute_type t;
};

static struct eagleplayerstore *playerstore;

static int ufcompare(const void *a, const void *b);
static struct eagleplayerstore *read_eagleplayer_conf(const char *filename);

static struct eagleplayer *analyze_file_format(int *content,
					       const char *modulename,
					       const char *basedir,
					       int verbose)
{
	struct stat st;
	char extension[16];

	FILE *f;
	size_t readed;
	struct eagleplayer *candidate;
	char *t, *tn;
	int len;
	static int warnings = 1;
	size_t bufsize;
	uint8_t fileformat_buf[8192];

	*content = 0;

	if ((f = fopen(modulename, "rb")) == NULL)
		return NULL;

	if (fstat(fileno(f), &st)) {
		fprintf(stderr, "Very weird stat error: %s (%s)\n", modulename,
			strerror(errno));
		exit(-1);
	}
	bufsize = sizeof fileformat_buf;
	readed = atomic_fread(fileformat_buf, 1, bufsize, f);
	fclose(f);
	if (readed == 0)
		return NULL;
	memset(&fileformat_buf[readed], 0, bufsize - readed);
	bufsize = readed;

	uade_filemagic(fileformat_buf, bufsize, extension, st.st_size, verbose);

	if (verbose)
		fprintf(stderr, "%s: deduced extension: %s\n", modulename,
			extension);

	if (strcmp(extension, "packed") == 0)
		return NULL;

	if (playerstore == NULL) {
		char formatsfile[PATH_MAX];
		snprintf(formatsfile, sizeof(formatsfile),
			 "%s/eagleplayer.conf", basedir);
		if ((playerstore = read_eagleplayer_conf(formatsfile)) == NULL) {
			if (warnings)
				fprintf(stderr,
					"Tried to load eagleplayer.conf from %s, but failed\n",
					formatsfile);
			warnings = 0;
			return NULL;
		}
		if (verbose)
			fprintf(stderr, "Loaded eagleplayer.conf: %s\n",
				formatsfile);
	}

	/* if filemagic found a match, we'll use player plugins associated with
	   that extension. if filemagic didn't find a match, we'll try to parse
	   pre- and postfixes from the modulename */

	if (extension[0]) {

		candidate = uade_get_eagleplayer(extension, playerstore);

		if (candidate) {
			*content = 1;
			return candidate;
		}

		if (verbose)
			fprintf(stderr,
				"Deduced file extension (%s) is not on eagleplayer.conf.\n",
				extension);
	}

	/* magic wasn't able to deduce the format, so we'll try prefix and postfix
	   from modulename */
	t = strrchr(modulename, (int)'/');
	if (t == NULL) {
		t = (char *)modulename;
	} else {
		t++;
	}

	/* try prefix first */
	tn = strchr(t, '.');
	if (tn == NULL)
		return NULL;

	len = ((intptr_t) tn) - ((intptr_t) t);
	if (len < sizeof(extension)) {
		memcpy(extension, t, len);
		extension[len] = 0;

		if ((candidate =
		     uade_get_eagleplayer(extension, playerstore)) != NULL)
			return candidate;
	}

	/* prefix didn't match anything. trying postfix */
	t = strrchr(t, '.');
	if (strlcpy(extension, t + 1, sizeof(extension)) >= sizeof(extension)) {
		/* too long to be an extension */
		return NULL;
	}

	if ((candidate = uade_get_eagleplayer(extension, playerstore)) != NULL)
		return candidate;

	return NULL;
}

int uade_parse_attribute(struct uade_attribute **attributelist, int *flags,
			 char *item, size_t lineno)
{
	size_t i;
	size_t len;

	struct attrlist esattrs[] = {
		{.s = "a500",.e = ES_A500},
		{.s = "a1200",.e = ES_A1200},
		{.s = "always_ends",.e = ES_ALWAYS_ENDS},
		{.s = "broken_song_end",.e = ES_BROKEN_SONG_END},
		{.s = "detect_format_by_content",.e = ES_CONTENT_DETECTION},
		{.s = "ignore_player_check",.e = ES_IGNORE_PLAYER_CHECK},
		{.s = "led_off",.e = ES_LED_OFF},
		{.s = "led_on",.e = ES_LED_ON},
		{.s = "never_ends",.e = ES_NEVER_ENDS},
		{.s = "no_ep_end_detect",.e = ES_BROKEN_SONG_END},
		{.s = "no_filter",.e = ES_NO_FILTER},
		{.s = "no_headphones",.e = ES_NO_HEADPHONES},
		{.s = "no_panning",.e = ES_NO_PANNING},
		{.s = "no_postprocessing",.e = ES_NO_POSTPROCESSING},
		{.s = "ntsc",.e = ES_NTSC},
		{.s = "one_subsong",.e = ES_ONE_SUBSONG},
		{.s = "pal",.e = ES_PAL},
		{.s = "reject",.e = ES_REJECT},
		{.s = "speed_hack",.e = ES_SPEED_HACK},
		{.s = NULL}
	};

	struct attrlist esvalueattrs[] = {
		{.s = "epopt",.t = UA_STRING,.e = ES_EP_OPTION},
		{.s = "gain",.t = UA_STRING,.e = ES_GAIN},
		{.s = "interpolator",.t = UA_STRING,.e = ES_RESAMPLER},
		{.s = "panning",.t = UA_STRING,.e = ES_PANNING},
		{.s = "player",.t = UA_STRING,.e = ES_PLAYER},
		{.s = "resampler",.t = UA_STRING,.e = ES_RESAMPLER},
		{.s = "silence_timeout",.t = UA_STRING,.e = ES_SILENCE_TIMEOUT},
		{.s = "subsong_timeout",.t = UA_STRING,.e = ES_SUBSONG_TIMEOUT},
		{.s = "subsongs",.t = UA_STRING,.e = ES_SUBSONGS},
		{.s = "timeout",.t = UA_STRING,.e = ES_TIMEOUT},
		{.s = NULL}
	};

	for (i = 0; esattrs[i].s != NULL; i++) {
		if (strcasecmp(item, esattrs[i].s) == 0) {
			*flags |= esattrs[i].e;
			return 1;
		}
	}

	for (i = 0; esvalueattrs[i].s != NULL; i++) {
		len = strlen(esvalueattrs[i].s);
		if (strncasecmp(item, esvalueattrs[i].s, len) == 0) {
			struct uade_attribute *a;
			char *str;
			char *endptr;
			int success;

			if (item[len] != '=') {
				fprintf(stderr, "Invalid song item: %s\n",
					item);
				break;
			}
			str = item + len + 1;

			if ((a = calloc(1, sizeof *a)) == NULL)
				eperror("No memory for song attribute.\n");

			success = 0;

			switch (esvalueattrs[i].t) {
			case UA_DOUBLE:
				a->d = strtod(str, &endptr);
				if (*endptr == 0)
					success = 1;
				break;
			case UA_INT:
				a->i = strtol(str, &endptr, 10);
				if (*endptr == 0)
					success = 1;
				break;
			case UA_STRING:
				a->s = strdup(str);
				if (a->s == NULL)
					eperror
					    ("Out of memory allocating string option for song\n");
				success = 1;
				break;
			default:
				fprintf(stderr, "Unknown song option: %s\n",
					item);
				break;
			}

			if (success) {
				a->type = esvalueattrs[i].e;
				a->next = *attributelist;
				*attributelist = a;
			} else {
				fprintf(stderr, "Invalid song option: %s\n",
					item);
				free(a);
			}

			return 1;
		}
	}

	return 0;
}

/* Split line with respect to white space. */
char **uade_split_line(size_t * nitems, size_t * lineno, FILE * f,
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

	if ((items = malloc(sizeof(items[0]) * (*nitems + 1))) == NULL) {
		fprintf(stderr, "No memory for nws items.\n");
		exit(-1);
	}

	sp = line;
	pos = 0;
	while ((s = strsep(&sp, delimiters)) != NULL) {
		if (*s == 0)
			continue;
		if ((items[pos] = strdup(s)) == NULL) {
			fprintf(stderr, "No memory for an nws item.\n");
			exit(-1);
		}
		pos++;
	}
	items[pos] = NULL;
	assert(pos == *nitems);

	return items;
}

/* Compare function for bsearch() and qsort() to sort eagleplayers with
   respect to name extension. */
static int ufcompare(const void *a, const void *b)
{
	const struct eagleplayermap *ua = a;
	const struct eagleplayermap *ub = b;
	return strcasecmp(ua->extension, ub->extension);
}

struct eagleplayer *uade_analyze_file_format(const char *modulename,
					     struct uade_config *uc)
{
	struct eagleplayer *ep;
	int content;

	ep = analyze_file_format(&content, modulename, uc->basedir.name,
				 uc->verbose);

	if (ep == NULL)
		return NULL;

	if (content)
		return ep;

	if (uc->content_detection && content == 0)
		return NULL;

	if ((ep->flags & ES_CONTENT_DETECTION) != 0)
		return NULL;

	return ep;
}

struct eagleplayer *uade_get_eagleplayer(const char *extension,
					 struct eagleplayerstore *ps)
{
	struct eagleplayermap *uf = ps->map;
	struct eagleplayermap *f;
	struct eagleplayermap key = {.extension = (char *)extension };

	f = bsearch(&key, uf, ps->nextensions, sizeof(uf[0]), ufcompare);
	if (f == NULL)
		return NULL;

	return f->player;
}

/* Read eagleplayer.conf. */
static struct eagleplayerstore *read_eagleplayer_conf(const char *filename)
{
	FILE *f;
	struct eagleplayer *p;
	size_t allocated;
	size_t lineno = 0;
	struct eagleplayerstore *ps = NULL;
	size_t exti;
	size_t i, j;
	int epwarning;

	f = fopen(filename, "r");
	if (f == NULL)
		goto error;

	ps = calloc(1, sizeof ps[0]);
	if (ps == NULL)
		eperror("No memory for ps.");

	allocated = 16;
	if ((ps->players = malloc(allocated * sizeof(ps->players[0]))) == NULL)
		eperror("No memory for eagleplayer.conf file.\n");

	while (1) {
		char **items;
		size_t nitems;

		if ((items =
		     uade_split_line(&nitems, &lineno, f,
				     UADE_WS_DELIMITERS)) == NULL)
			break;

		assert(nitems > 0);

		if (ps->nplayers == allocated) {
			allocated *= 2;
			ps->players =
			    realloc(ps->players,
				    allocated * sizeof(ps->players[0]));
			if (ps->players == NULL)
				eperror("No memory for players.");
		}

		p = &ps->players[ps->nplayers];
		ps->nplayers++;

		memset(p, 0, sizeof p[0]);

		p->playername = strdup(items[0]);
		if (p->playername == NULL) {
			fprintf(stderr, "No memory for playername.\n");
			exit(-1);
		}

		for (i = 1; i < nitems; i++) {

			if (strncasecmp(items[i], "prefixes=", 9) == 0) {
				char prefixes[LINESIZE];
				char *prefixstart = items[i] + 9;
				char *sp, *s;
				size_t pos;

				assert(p->nextensions == 0
				       && p->extensions == NULL);

				p->nextensions = 0;
				strlcpy(prefixes, prefixstart,
					sizeof(prefixes));
				sp = prefixes;
				while ((s =
					strsep(&sp,
					       OPTION_DELIMITER)) != NULL) {
					if (*s == 0)
						continue;
					p->nextensions++;
				}

				p->extensions =
				    malloc((p->nextensions +
					    1) * sizeof(p->extensions[0]));
				if (p->extensions == NULL)
					eperror("No memory for extensions.");

				pos = 0;
				sp = prefixstart;
				while ((s =
					strsep(&sp,
					       OPTION_DELIMITER)) != NULL) {
					if (*s == 0)
						continue;

					p->extensions[pos] = strdup(s);
					if (s == NULL)
						eperror
						    ("No memory for prefix.");
					pos++;
				}
				p->extensions[pos] = NULL;
				assert(pos == p->nextensions);

				continue;
			}

			if (strncasecmp(items[i], "comment:", 7) == 0)
				break;

			if (uade_parse_attribute
			    (&p->attributelist, &p->flags, items[i], lineno))
				continue;

			fprintf(stderr, "Unrecognized option: %s\n", items[i]);
		}

		for (i = 0; items[i] != NULL; i++)
			free(items[i]);

		free(items);
	}

	fclose(f);

	if (ps->nplayers == 0) {
		free(ps->players);
		free(ps);
		return NULL;
	}

	for (i = 0; i < ps->nplayers; i++)
		ps->nextensions += ps->players[i].nextensions;

	ps->map = malloc(sizeof(ps->map[0]) * ps->nextensions);
	if (ps->map == NULL)
		eperror("No memory for extension map.");

	exti = 0;
	epwarning = 0;
	for (i = 0; i < ps->nplayers; i++) {
		p = &ps->players[i];
		if (p->nextensions == 0) {
			if (epwarning == 0) {
				fprintf(stderr,
					"uade warning: %s eagleplayer lacks prefixes in "
					"eagleplayer.conf, which makes it unusable for any kind of "
					"file type detection. If you don't want name based file type "
					"detection for a particular format, use content_detection "
					"option for the line in eagleplayer.conf.\n",
					ps->players[i].playername);
				epwarning = 1;
			}
			continue;
		}
		for (j = 0; j < p->nextensions; j++) {
			assert(exti < ps->nextensions);
			ps->map[exti].player = p;
			ps->map[exti].extension = p->extensions[j];
			exti++;
		}
	}

	assert(exti == ps->nextensions);

	qsort(ps->map, ps->nextensions, sizeof(ps->map[0]), ufcompare);

	return ps;

 error:
	if (ps)
		free(ps->players);
	free(ps);
	if (f != NULL)
		fclose(f);
	return NULL;
}
