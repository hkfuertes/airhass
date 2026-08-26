/*
 * test_ha_now_playing.c — tiny check for the now-playing state payload.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/ha_api.h"

int main(void) {
	char payload[2048];

	assert(ha_build_now_playing_payload("playing",
	                                    "Macaco", "Hijos de un Mismo Dios", "Hijos de un Mismo Dios",
	                                    "http://192.168.1.10:1234/ab12.jpg",
	                                    payload, sizeof(payload)));
	assert(strstr(payload, "\"state\":\"playing\""));
	assert(strstr(payload, "\"media_artist\":\"Macaco\""));
	assert(strstr(payload, "\"media_album_name\":\"Hijos de un Mismo Dios\""));
	assert(strstr(payload, "\"media_title\":\"Hijos de un Mismo Dios\""));
	assert(strstr(payload, "\"entity_picture\":\"http://192.168.1.10:1234/ab12.jpg\""));

	/* optional fields omitted */
	assert(ha_build_now_playing_payload("paused", NULL, NULL, "Solo title", NULL, payload, sizeof(payload)));
	assert(strstr(payload, "\"state\":\"paused\""));
	assert(strstr(payload, "\"media_title\":\"Solo title\""));
	assert(!strstr(payload, "media_artist"));
	assert(!strstr(payload, "entity_picture"));

	/* state is mandatory */
	assert(!ha_build_now_playing_payload("", NULL, NULL, "t", NULL, payload, sizeof(payload)));
	assert(!ha_build_now_playing_payload(NULL, NULL, NULL, "t", NULL, payload, sizeof(payload)));

	puts("PASS test_ha_now_playing");
	return 0;
}
