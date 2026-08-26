/*
 * test_ha_now_playing.c — tiny check for the now-playing state payload.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/ha_api.h"

int main(void) {
	char payload[2048];

	assert(ha_build_now_playing_payload("Macaco - Hijos de un Mismo Dios",
	                                    "Macaco", "Hijos de un Mismo Dios", "Hijos de un Mismo Dios",
	                                    "media_player.kitchen", "http://192.168.1.10:1234/ab12.jpg",
	                                    payload, sizeof(payload)));
	assert(strstr(payload, "\"state\":\"Macaco - Hijos de un Mismo Dios\""));
	assert(strstr(payload, "\"artist\":\"Macaco\""));
	assert(strstr(payload, "\"album\":\"Hijos de un Mismo Dios\""));
	assert(strstr(payload, "\"title\":\"Hijos de un Mismo Dios\""));
	assert(strstr(payload, "\"media_player\":\"media_player.kitchen\""));
	assert(strstr(payload, "\"entity_picture\":\"http://192.168.1.10:1234/ab12.jpg\""));

	/* optional fields omitted */
	assert(ha_build_now_playing_payload("Solo title", NULL, NULL, "Solo title",
	                                    "media_player.kitchen", NULL, payload, sizeof(payload)));
	assert(strstr(payload, "\"state\":\"Solo title\""));
	assert(!strstr(payload, "artist"));
	assert(!strstr(payload, "entity_picture"));

	/* state is mandatory */
	assert(!ha_build_now_playing_payload("", NULL, NULL, "t", NULL, NULL, payload, sizeof(payload)));
	assert(!ha_build_now_playing_payload(NULL, NULL, NULL, "t", NULL, NULL, payload, sizeof(payload)));

	puts("PASS test_ha_now_playing");
	return 0;
}
