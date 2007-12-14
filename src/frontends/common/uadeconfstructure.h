#ifndef _UADECONF_STRUCTURE_H_
#define _UADECONF_STRUCTURE_H_

#include <limits.h>

enum uade_option {
	UC_ACTION_KEYS = 0x1000,
	UC_BASE_DIR,
	UC_BUFFER_TIME,
	UC_CONTENT_DETECTION,
	UC_DISABLE_TIMEOUTS,
	UC_ENABLE_TIMEOUTS,
	UC_EAGLEPLAYER_OPTION,
	UC_FILTER_TYPE,
	UC_FORCE_LED_OFF,
	UC_FORCE_LED_ON,
	UC_FORCE_LED,
	UC_FREQUENCY,
	UC_GAIN,
	UC_HEADPHONES,
	UC_HEADPHONES2,
	UC_IGNORE_PLAYER_CHECK,
	UC_NO_FILTER,
	UC_NO_HEADPHONES,
	UC_NO_PANNING,
	UC_NO_POSTPROCESSING,
	UC_NO_EP_END,
	UC_NORMALISE,
	UC_NTSC,
	UC_ONE_SUBSONG,
	UC_PAL,
	UC_PANNING_VALUE,
	UC_RANDOM_PLAY,
	UC_RECURSIVE_MODE,
	UC_RESAMPLER,
	UC_SILENCE_TIMEOUT_VALUE,
	UC_SONG_TITLE,
	UC_SPEED_HACK,
	UC_SUBSONG_TIMEOUT_VALUE,
	UC_TIMEOUT_VALUE,
	UC_USE_TEXT_SCOPE,
	UC_VERBOSE
};

struct uade_dir {
	char name[PATH_MAX];
};

struct uade_ep_options {
	char o[256];
	size_t s;
};

/* All the options are put into an instance of this structure.
 * There can be many structures, one for uade.conf and the other for
 * command line options. Then these structures are then merged together
 * to know the complete behavior for each case. Note, these structures
 * can be conflicting, so the options are merged in following order
 * so that the last merge will determine true behavior:
 *
 *     1. set uade.conf options
 *     2. set eagleplayer attributes
 *     3. set song attributes
 *     4. set command line options
 *
 * Merging works by looking at X_set members of this structure. X_set
 * member indicates that feature X has explicitly been set, so the
 * merge will notice the change in value.
 */
struct uade_config {
	char action_keys;
	char action_keys_set;

	struct uade_dir basedir;
	char basedir_set;

	int buffer_time;
	char buffer_time_set;

	char content_detection;
	char content_detection_set;

	struct uade_ep_options ep_options;
	char ep_options_set;

	char filter_type;
	char filter_type_set;

	int frequency;
	char frequency_set;

	char led_forced;
	char led_forced_set;
	char led_state;
	char led_state_set;

	char gain_enable;
	char gain_enable_set;
	float gain;		/* should be removed of uade_effect integrated */
	char gain_set;

	char headphones;
	char headphones_set;
	char headphones2;
	char headphones2_set;

	char ignore_player_check;
	char ignore_player_check_set;

	char *resampler;
	char resampler_set;

	char no_ep_end;
	char no_ep_end_set;

	char no_filter;
	char no_filter_set;

	char no_postprocessing;
	char no_postprocessing_set;

	char normalise;
	char normalise_set;
	char *normalise_parameter;	/* no normalise_parameter_set entry, use manual
					   merging code */

	char one_subsong;
	char one_subsong_set;

	float panning;		/* should be removed */
	char panning_set;
	char panning_enable;
	char panning_enable_set;

	char random_play;
	char random_play_set;

	char recursive_mode;
	char recursive_mode_set;

	int silence_timeout;
	char silence_timeout_set;

	char *song_title;
	char song_title_set;

	char speed_hack;
	char speed_hack_set;

	int subsong_timeout;
	char subsong_timeout_set;

	int timeout;
	char timeout_set;

	char use_text_scope;
	char use_text_scope_set;

	char use_timeouts;
	char use_timeouts_set;

	char use_ntsc;
	char use_ntsc_set;

	char verbose;
	char verbose_set;
};

#endif
