/*
 * test_ha_volume.c — tiny check for Home Assistant volume mapping/payload.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/ha_api.h"

int main(void) {
	char payload[256];

	assert(ha_volume_level(-1.0) == 0.0);
	assert(ha_volume_level(0.0) == 0.0);
	assert(ha_volume_level(0.5) == 0.5);
	assert(ha_volume_level(1.0) == 1.0);
	assert(ha_volume_level(2.0) == 1.0);

	assert(ha_build_volume_payload("media_player.kitchen", 1.5, payload, sizeof(payload)));
	assert(strstr(payload, "\"entity_id\":\"media_player.kitchen\""));
	assert(strstr(payload, "\"volume_level\":1.0"));

	assert(ha_build_volume_payload("media_player.kitchen", -1.0, payload, sizeof(payload)));
	assert(strstr(payload, "\"volume_level\":0.0"));

	puts("PASS test_ha_volume");
	return 0;
}
