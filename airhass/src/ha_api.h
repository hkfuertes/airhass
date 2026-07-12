/*
 *  AirHass: Home Assistant API minimal client
 *
 *  (c) AirHass contributors
 *
 * See LICENSE
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#define HA_ENTITY_ID_LEN 256
#define HA_ENTITY_NAME_LEN 256
#define HA_ENTITY_UDN_LEN 256
#define HA_STATE_LEN 32

typedef struct {
	char host[256];
	int  port;
	char path[512];
} ha_url_t;

typedef struct {
	char entity_id[HA_ENTITY_ID_LEN];
	char name[HA_ENTITY_NAME_LEN];
	char udn[HA_ENTITY_UDN_LEN];
} ha_entity_t;

typedef struct {
	char state[HA_STATE_LEN];
	bool has_volume_level;
	double volume_level;
} ha_media_player_state_t;

typedef enum {
	HA_RAOP_NONE = 0,
	HA_RAOP_PLAY,
	HA_RAOP_PAUSE,
	HA_RAOP_STOP,
} ha_raop_event_t;

/* Parse http://host[:port][/path] into *out.
 * Returns false on bad scheme, empty url, or buffer overflow.
 * ponytail: https:// not supported; add cross_ssl BIO layer when needed */
bool ha_url_parse(const char *url, ha_url_t *out);

/* GET /api/ with Bearer token; returns true on HTTP 200.
 * Returns false and prints to stderr on connect/auth failure.
 * ponytail: http:// only — sufficient for local HA installs */
bool ha_ping(const char *url, const char *token);

/* Parse Home Assistant /api/states JSON and keep only media_player.* entities.
 * Returns the number of entries written to out (up to max).
 * ponytail: only friendly_name is used for display; add more attributes when needed */
int ha_parse_media_players(const char *json, ha_entity_t *out, int max);

/* Fetch /api/states and return filtered media_player.* entities.
 * Returns count on success, -1 on fetch/parse failure. */
int ha_fetch_media_players(const char *url, const char *token, ha_entity_t *out, int max,
                           bool hide_cast);

/* Parse Home Assistant /api/states/<entity_id> JSON into transport state + volume.
 * Returns false on invalid JSON/object shape. */
bool ha_parse_media_player_state(const char *json, ha_media_player_state_t *out);

/* Fetch /api/states/<entity_id>.
 * Returns true on HTTP 2xx + valid JSON. */
bool ha_fetch_media_player_state(const char *url, const char *token, const char *entity_id,
                                 ha_media_player_state_t *out);

/* Map HA transport state to the next RAOP-side action.
 * ponytail: current is last known RAOP-facing state, enough to suppress repeats. */
ha_raop_event_t ha_state_to_raop_event(const char *state, ha_raop_event_t current);

/* Map codec config (e.g. flac, mp3:320, aac:256, wav) to stream URL extension.
 * Unknown/empty codecs fall back to flac. */
const char *ha_codec_extension(const char *codec_config);

/* Map codec config (e.g. flac, mp3:320, aac:256, wav) to MIME.
 * Unknown/empty codecs fall back to audio/flac. */
const char *ha_codec_content_type(const char *codec_config);

/* Build the JSON body for media_player.play_media.
 * Returns false on invalid args or buffer overflow. */
bool ha_build_play_media_payload(const char *entity_id, const char *media_content_id,
                                 const char *media_content_type, char *out, size_t out_len);

/* Build the JSON body for entity-targeted services like media_stop.
 * Returns false on invalid args or buffer overflow. */
bool ha_build_entity_payload(const char *entity_id, char *out, size_t out_len);

/* Clamp a Home Assistant volume level to the valid 0..1 range.
 * ponytail: AirPlay dB -> 0..1 conversion already happens in RAOP code. */
double ha_volume_level(double level);

/* Build the JSON body for media_player.volume_set.
 * Returns false on invalid args or buffer overflow. */
bool ha_build_volume_payload(const char *entity_id, double volume_level, char *out, size_t out_len);

/* POST /api/services/media_player/play_media.
 * Returns true on HTTP 2xx, false and logs actionable errors otherwise. */
bool ha_play_media(const char *url, const char *token, const char *entity_id,
                   const char *media_content_id, const char *media_content_type);

/* POST /api/services/media_player/media_stop. */
bool ha_stop_media(const char *url, const char *token, const char *entity_id);

/* POST /api/services/media_player/volume_set using 0..1 scale. */
bool ha_set_volume(const char *url, const char *token, const char *entity_id, double volume_level);
