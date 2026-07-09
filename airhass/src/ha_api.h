/*
 *  AirHass: Home Assistant API minimal client
 *
 *  (c) AirHass contributors
 *
 * See LICENSE
 */

#pragma once

#include <stdbool.h>

#define HA_ENTITY_ID_LEN 256
#define HA_ENTITY_NAME_LEN 256
#define HA_ENTITY_UDN_LEN 256

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
int ha_fetch_media_players(const char *url, const char *token, ha_entity_t *out, int max);
