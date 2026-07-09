/*
 * test_ha_play_media.c — tiny check for Home Assistant play_media payload.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/ha_api.h"

int main(void) {
	char payload[512];
	const char *entity_id = "media_player.kitchen";
	const char *url = "http://192.168.1.10:1234/stream-7.flac";

	assert(ha_build_play_media_payload(entity_id, url, "music", payload, sizeof(payload)));
	assert(strstr(payload, "\"entity_id\":\"media_player.kitchen\""));
	assert(strstr(payload, "\"media_content_id\":\"http://192.168.1.10:1234/stream-7.flac\""));
	assert(strstr(payload, "\"media_content_type\":\"music\""));

	puts("PASS test_ha_play_media");
	return 0;
}
