/* uade123 - a very very stupid simple command line frontend for uadecore.

   Copyright (C) 2007 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "uadecontrol.h"
#include "uadeipc.h"
#include "strlrep.h"
#include "uadeconfig.h"
#include "eagleplayer.h"
#include "uadeconf.h"
#include "sysincludes.h"
#include "songdb.h"

#include "uadesimple.h"
#include "playloop.h"
#include "audio.h"
#include "amigafilter.h"

int uade_song_end_trigger;

static char uadename[PATH_MAX];

static void cleanup(void);


int main(int argc, char *argv[])
{
    FILE *playerfile;
    char configname[PATH_MAX] = "";
    char playername[PATH_MAX] = "";
    char scorename[PATH_MAX] = "";
    int ret;
    int uadeconf_loaded;
    char uadeconfname[PATH_MAX];
    struct uade_effect effects;
    struct uade_config uc, uc_main;
    struct uade_ipc uadeipc;
    int songindex;
    int uadepid;

    uadeconf_loaded = uade_load_initial_config(uadeconfname,
					       sizeof uadeconfname,
					       &uc_main, NULL);

    /* Merge loaded configurations and command line options */
    uc = uc_main;

    if (uadeconf_loaded == 0) {
	debug(uc.verbose,
	      "Not able to load uade.conf from ~/.uade2/ or %s/.\n",
	      uc.basedir.name);
    } else {
	debug(uc.verbose, "Loaded configuration: %s\n", uadeconfname);
    }

    do {
	DIR *bd;
	if ((bd = opendir(uc.basedir.name)) == NULL) {
	    fprintf(stderr, "Could not access dir %s: %s\n",
		    uc.basedir.name, strerror(errno));
	    exit(1);
	}
	closedir(bd);

	snprintf(configname, sizeof configname, "%s/uaerc",
		 uc.basedir.name);

	if (scorename[0] == 0)
	    snprintf(scorename, sizeof scorename, "%s/score",
		     uc.basedir.name);

	if (uadename[0] == 0)
	    strlcpy(uadename, UADE_CONFIG_UADE_CORE, sizeof uadename);

	if (access(configname, R_OK)) {
	    fprintf(stderr, "Could not read %s: %s\n", configname,
		    strerror(errno));
	    exit(1);
	}
	if (access(scorename, R_OK)) {
	    fprintf(stderr, "Could not read %s: %s\n", scorename,
		    strerror(errno));
	    exit(1);
	}
	if (access(uadename, X_OK)) {
	    fprintf(stderr, "Could not execute %s: %s\n", uadename,
		    strerror(errno));
	    exit(1);
	}
    } while (0);

    uade_spawn(&uadeipc, &uadepid, uadename, configname);

    if (!audio_init(uc.frequency, uc.buffer_time))
	goto cleanup;

    for (songindex = 1; songindex < argc; songindex++) {
	struct uade_song *us;
	/* modulename and songname are a bit different. modulename is the name
	   of the song from uadecore's point of view and songname is the
	   name of the song from user point of view. Sound core considers all
	   custom songs to be players (instead of modules) and therefore modulename
	   will become a zero-string with custom songs. */
	char modulename[PATH_MAX];
	char songname[PATH_MAX];
	struct eagleplayer *ep = NULL;

	strlcpy(modulename, argv[songindex], sizeof modulename);

	uc = uc_main;

	ep = uade_analyze_file_format(modulename, &uc);
	if (ep == NULL) {
	    fprintf(stderr, "Unknown format: %s\n", modulename);
	    continue;
	}

	debug(uc.verbose, "Player candidate: %s\n", ep->playername);

	if (strcmp(ep->playername, "custom") == 0) {
	    strlcpy(playername, modulename, sizeof playername);
	    modulename[0] = 0;
	} else {
	  snprintf(playername, sizeof playername, "%s/players/%s", uc.basedir.name, ep->playername);
	}

	if (strlen(playername) == 0) {
	    fprintf(stderr, "Error: an empty player name given\n");
	    goto cleanup;
	}

	/* If no modulename given, try the playername as it can be a custom
	   song */
	strlcpy(songname, modulename[0] ? modulename : playername,
		sizeof songname);

	if ((us = uade_alloc_song(songname)) == NULL) {
	    fprintf(stderr, "Can not read %s: %s\n", songname,
		    strerror(errno));
	    continue;
	}

	/* The order of parameter processing is important:
	 * 0. set uade.conf options (done before this)
	 * 1. set eagleplayer attributes
	 * 2. set song attributes
	 * 3. set command line options
	 */

	if (ep != NULL)
	    uade_set_ep_attributes(&uc, us, ep);

	/* Now we have the final configuration in "uc". */
	uade_set_effects(&effects, &uc);

	playerfile = fopen(playername, "r");
	if (playerfile == NULL) {
	    fprintf(stderr, "Can not find player: %s (%s)\n", playername,
		    strerror(errno));
	    uade_unalloc_song(us);
	    continue;
	}
	fclose(playerfile);

	debug(uc.verbose, "Player: %s\n", playername);

	fprintf(stderr, "Song: %s (%zd bytes)\n", us->module_filename, us->bufsize);

	ret = uade_song_initialization(scorename, playername, modulename,
				       us, &uadeipc, &uc);
	if (ret) {
	    if (ret == UADECORE_INIT_ERROR) {
		uade_unalloc_song(us);
		goto cleanup;

	    } else if (ret == UADECORE_CANT_PLAY) {
		debug(uc.verbose, "Uadecore refuses to play the song.\n");
		uade_unalloc_song(us);
		continue;
	    }

	    fprintf(stderr, "Unknown error from uade_song_initialization()\n");
	    exit(1);
	}

	play_loop(&uadeipc, us, &effects, &uc);

	uade_unalloc_song(us);

	uade_song_end_trigger = 0;
    }

    cleanup();
    return 0;

  cleanup:
    cleanup();
    return 1;
}


static void cleanup(void)
{
    audio_close();
}
