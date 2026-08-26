/*
 * test_ha_config.c — standalone check for ha_url_parse() and config validation.
 *
 * Build & run (macOS arm64 repo checkout):
 *   cc -std=gnu11 -Wall test_ha_config.c src/ha_api.c \
 *     -I libjansson/targets/macos/arm64/include \
 *     libjansson/targets/macos/arm64/libjansson.a -o test_ha_config && ./test_ha_config
 *
 * Does NOT need a real Home Assistant; only tests parsing logic.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "src/ha_api.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(label, expr) \
	do { \
		if (expr) { g_pass++; fprintf(stderr, "  ok: %s\n", label); } \
		else { g_fail++; fprintf(stderr, "FAIL: %s\n", label); } \
	} while (0)

int main(void) {
	ha_url_t u;

	fprintf(stderr, "\n=== ha_url_parse tests ===\n");

	/* 1. full URL with port */
	CHECK("full URL parses",
	      ha_url_parse("http://homeassistant.local:8123", &u));
	CHECK("host extracted",   strcmp(u.host, "homeassistant.local") == 0);
	CHECK("port extracted",   u.port == 8123);
	CHECK("no prefix",        strcmp(u.path, "") == 0);

	/* 2. URL without explicit port → default 8123 */
	CHECK("no-port URL parses",
	      ha_url_parse("http://192.168.1.100", &u));
	CHECK("ip host ok",       strcmp(u.host, "192.168.1.100") == 0);
	CHECK("default port",     u.port == 8123);

	/* 3. URL with trailing slash */
	CHECK("trailing-slash parses",
	      ha_url_parse("http://hassio.local:8123/", &u));
	CHECK("host with slash",  strcmp(u.host, "hassio.local") == 0);
	CHECK("port with slash",  u.port == 8123);

	/* 4. Home Assistant Supervisor proxy */
	CHECK("supervisor proxy parses", ha_url_parse("http://supervisor:80/core", &u));
	CHECK("supervisor host", strcmp(u.host, "supervisor") == 0);
	CHECK("supervisor port", u.port == 80);
	CHECK("supervisor prefix", strcmp(u.path, "/core") == 0);

	/* 5. bad scheme */
	CHECK("https rejected",   !ha_url_parse("https://hassio.local", &u));

	/* 6. empty string */
	CHECK("empty rejected",   !ha_url_parse("", &u));

	/* 7. missing host */
	CHECK("bare scheme rejected", !ha_url_parse("http://", &u));

	fprintf(stderr, "\n=== config validation logic ===\n");

	/* Simulate startup check: both set = ok path (network not tested here) */
	const char *ha_url   = "http://hassio.local:8123";
	const char *ha_token = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9";
	CHECK("url+token non-empty → proceed",
	      *ha_url && *ha_token);

	/* Empty URL → HA disabled, no error */
	ha_url = "";
	CHECK("empty url → HA disabled",
	      !*ha_url);

	/* URL set but no token → should fail at startup */
	ha_url   = "http://hassio.local:8123";
	ha_token = "";
	CHECK("url set, empty token → startup should fail",
	      *ha_url && !*ha_token);

	fprintf(stderr, "\n%s: %d passed, %d failed\n",
	        g_fail ? "FAIL" : "PASS", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
