/*
 *  AirHass: Home Assistant API minimal client
 *
 *  (c) AirHass contributors
 *
 * See LICENSE
 */

#pragma once

#include <stdbool.h>

typedef struct {
	char host[256];
	int  port;
	char path[512];
} ha_url_t;

/* Parse http://host[:port][/path] into *out.
 * Returns false on bad scheme, empty url, or buffer overflow.
 * ponytail: https:// not supported; add cross_ssl BIO layer when needed */
bool ha_url_parse(const char *url, ha_url_t *out);

/* GET /api/ with Bearer token; returns true on HTTP 200.
 * Returns false and prints to stderr on connect/auth failure.
 * ponytail: http:// only — sufficient for local HA installs */
bool ha_ping(const char *url, const char *token);
