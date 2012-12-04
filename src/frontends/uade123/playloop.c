/* uade123 - a simple command line frontend for uadecore.

   Copyright (C) 2005-2010 Heikki Orsila <heikki.orsila@iki.fi>

   This source code module is dual licensed under GPL and Public Domain.
   Hence you may use _this_ module (not another code module) in any way you
   want in your projects.
*/

#include <uade/uade.h>
#include "playloop.h"
#include "uade123.h"
#include "audio.h"
#include "terminal.h"
#include "playlist.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>


static int cursormode;


static void print_song_info(struct uade_state *state, enum song_info_type t)
{
	char infotext[16384];
	FILE *f = uade_terminal_file ? uade_terminal_file : stdout;
	const struct uade_song_info *info = uade_get_song_info(state);
	if (!uade_song_info(infotext, sizeof infotext, info->modulefname, t))
		fprintf(f, "\n%s\n", infotext);
}

static void print_info(struct uade_state *state)
{
	const struct uade_song_info *info = uade_get_song_info(state);

	if (uade_info_mode) {
		tprintf("formatname: %s\n", info->formatname);
		tprintf("modulename: %s\n", info->modulename);
		tprintf("playername: %s\n", info->playername);
		tprintf("subsongs: cur %d min %d max %d\n", info->subsongs.cur, info->subsongs.min, info->subsongs.max);
	} else {
		int n = 1 + info->subsongs.max - info->subsongs.min;
		uade_debug(state, "Format name: %s\n", info->formatname);
		uade_debug(state, "Module name: %s\n", info->modulename);
		uade_debug(state, "Player name: %s\n", info->playername);
		if (n > 1)
			fprintf(stderr, "There are %d subsongs in range [%d, %d].\n", n, info->subsongs.min, info->subsongs.max);
	}
}

static void print_time(struct uade_state *state)
{
	const struct uade_song_info *info = &state->song.info;
	int deciseconds = (info->subsongbytes * 10) / info->bytespersecond;
	if (uade_no_text_output)
		return;
	tprintf("Playing time position %d.%ds in subsong %d / %d ", deciseconds / 10, deciseconds % 10,  info->subsongs.cur == -1 ? 0 : info->subsongs.cur, info->subsongs.max);
	if (info->playtime >= 0) {
		int ptimesecs = info->playtime / 1000;
		int ptimesubsecs = (info->playtime / 100) % 10;
		tprintf("(all subs %d.%ds)  \r", ptimesecs, ptimesubsecs);
	} else {
		tprintf("                   \r");
	}
	fflush(stdout);
}

int terminal_input(int *plistdir, struct uade_state *state)
{
	int newsub;
	int ret;
	const struct uade_song_info *info;

	info = uade_get_song_info(state);

	ret = read_terminal();
	switch (ret) {
	case 0:
		break;
	case '<':
		*plistdir = UADE_PLAY_PREVIOUS;
		return -1;
	case '>':
		*plistdir = UADE_PLAY_NEXT;
		return -1;

	case UADE_CURSOR_LEFT:
		uade_seek(UADE_SEEK_POSITION_RELATIVE, -10, 0, state);
		break;
	case UADE_CURSOR_RIGHT:
	case '.':
		uade_seek(UADE_SEEK_POSITION_RELATIVE, 10, 0, state);
		break;
	case UADE_CURSOR_DOWN:
		uade_seek(UADE_SEEK_POSITION_RELATIVE, -60, 0, state);
		break;
	case UADE_CURSOR_UP:
		uade_seek(UADE_SEEK_POSITION_RELATIVE, 60, 0, state);
		break;

	case 'b':
		newsub = info->subsongs.cur + 1;
		if (newsub > info->subsongs.max) {
			*plistdir = UADE_PLAY_NEXT;
			return -1;
		}
		if (uade_seek(UADE_SEEK_SUBSONG_RELATIVE, 0, newsub, state))
			tprintf("\nBad subsong number: %d\n", newsub);
		break;
	case ' ':
	case 'c':
		pause_terminal();
		break;
	case 'f':
		uade_config_set_option(&state->config, UC_FORCE_LED, state->config.led_state ? "off" : "on");
		tprintf("\nForcing LED %s\n", (state->config.led_state & 1) ? "ON" : "OFF");
		uade_send_filter_command(state);
		break;
	case 'g':
		uade_effect_toggle(&state->effectstate, UADE_EFFECT_GAIN);
		tprintf("\nGain effect %s %s\n", uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_GAIN) ? "ON" : "OFF", (uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_ALLOW) == 0 && uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_GAIN)) ? "(Remember to turn ON postprocessing!)" : "");
		break;
	case 'h':
		tprintf("\n\n");
		print_action_keys();
		tprintf("\n");
		break;
	case 'H':
		uade_effect_toggle(&state->effectstate, UADE_EFFECT_HEADPHONES);
		tprintf("\nHeadphones effect %s %s\n", uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_HEADPHONES) ? "ON" : "OFF", (uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_ALLOW) == 0 && uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_HEADPHONES) == 1) ? "(Remember to turn ON postprocessing!)" : "");
		break;
	case 'i':
		if (!uade_no_text_output)
			print_song_info(state, UADE_MODULE_INFO);
		break;
	case 'I':
		if (!uade_no_text_output)
			print_song_info(state, UADE_HEX_DUMP_INFO);
		break;
	case '\n':
		*plistdir = UADE_PLAY_NEXT;
		return -1;
	case 'p':
		uade_effect_toggle(&state->effectstate, UADE_EFFECT_ALLOW);
		tprintf("\nPostprocessing effects %s\n", uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_ALLOW) ? "ON" : "OFF");
		break;
	case 'P':
		uade_effect_toggle(&state->effectstate, UADE_EFFECT_PAN);
		tprintf("\nPanning effect %s %s\n", uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_PAN) ? "ON" : "OFF", (uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_ALLOW) == 0 && uade_effect_is_enabled(&state->effectstate, UADE_EFFECT_PAN) == 1) ? "(Remember to turn ON postprocessing!)" : "");
		break;
	case 'q':
		*plistdir = UADE_PLAY_EXIT;
		return -1;
	case 's':
		playlist_random(&uade_playlist, -1);
		tprintf("\n%s mode\n", uade_playlist.randomize ? "Shuffle" : "Normal");
		break;
	case 'v':
		state->config.verbose ^= 1;
		tprintf("\nVerbose mode %s\n", state->config.verbose ? "ON" : "OFF");
		break;
	case 'x':
		uade_seek(UADE_SEEK_SUBSONG_RELATIVE, 0, info->subsongs.cur, state);
		break;
	case 'z':
		newsub = info->subsongs.cur - 1;
		if (newsub < 0 || info->subsongs.cur <= info->subsongs.min) {
			*plistdir = UADE_PLAY_PREVIOUS;
			return -1;
		}
		if (uade_seek(UADE_SEEK_SUBSONG_RELATIVE, 0, newsub, state))
			tprintf("\nBad subsong number: %d\n", newsub);
		break;
	default:
		if (isdigit(ret)) {
			newsub = ret - '0';
			if (uade_seek(UADE_SEEK_SUBSONG_RELATIVE, 0, newsub, state))
				tprintf("\nBad subsong number\n");
		} else if (!isspace(ret)) {
			fprintf(stderr, "\nKey '%c' is not a valid command (hex 0x%.2x)\n", ret, ret);
		}
	}
	return 0;
}

int uade_input(int *plistdir, struct uade_state *state)
{
	struct uade_event event;

	test_and_trigger_debug(state);

	while (1) {
		if (uade_get_event(&event, state)) {
			fprintf(stderr, "uade_get_event(): error!\n");
			*plistdir = UADE_PLAY_FAILURE;
			break;
		}
		
		switch (event.type) {
		case UADE_EVENT_EAGAIN:
			return 0;
		case UADE_EVENT_DATA:
			audio_play((char *) event.data.data, event.data.size);
			print_time(state);
			break;
		case UADE_EVENT_MESSAGE:
			tprintf("\n%s\n", event.msg);
			break;
		case UADE_EVENT_SONG_END:
			tprintf("\n%s: %s\n", event.songend.happy ? "song end" : "bad song end", event.songend.reason);
			if (!event.songend.happy || event.songend.stopnow)
				return -1;
			if (uade_next_subsong(state))
				return -1;
			break;
		default:
			fprintf(stderr, "uade_get_event() returned %s which is not handled.\n", uade_event_name(&event));
			*plistdir = UADE_PLAY_FAILURE;
			return -1;
		}
	}
	return -1;
}

int play_loop(struct uade_state *state)
{
	int plistdir = UADE_PLAY_NEXT;
	int fds[2] = {uade_get_fd(state), -1};
	int maxfd = fds[0];
	fd_set fdset;
	int ret;

	if (terminal_fd >= 0 && actionkeys) {
		fds[1] = terminal_fd;
		if (fds[1] > maxfd)
			maxfd = fds[1];
	}

	print_info(state);
	if (uade_info_mode)
		return plistdir;

	if (uade_jump_pos > 0)
		uade_seek(UADE_SEEK_SONG_RELATIVE, uade_jump_pos, 0, state);

	while (1) {
		FD_ZERO(&fdset);
		FD_SET(fds[0], &fdset);
		if (fds[1] >= 0)
			FD_SET(fds[1], &fdset);

		ret = select(maxfd + 1, &fdset, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			uade_warning("select error: %s\n", strerror(errno));
			break;
		}
		if (ret == 0)
			continue;
		if (FD_ISSET(fds[0], &fdset) &&  uade_input(&plistdir, state))
			break;
		if (fds[1] >= 0 && FD_ISSET(fds[1], &fdset)) {
			if (terminal_input(&plistdir, state))
				break;
		}
	}

	tprintf("\n");

	return plistdir;
}
