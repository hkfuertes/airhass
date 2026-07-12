/*
 * test_ha_entities.c — tiny check for Home Assistant media_player filtering.
 *
 * Build & run (macOS arm64 repo checkout):
 *   cc -std=gnu11 -Wall test_ha_entities.c src/ha_api.c \
 *     -I src -I libjansson/targets/macos/arm64/include \
 *     libjansson/targets/macos/arm64/libjansson.a -o test_ha_entities && ./test_ha_entities
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/ha_api.h"

int main(void) {
	static const char *json =
		"["
		"{\"entity_id\":\"media_player.living_room\",\"attributes\":{\"friendly_name\":\"Living Room\"}},"
		"{\"entity_id\":\"sensor.outdoor_temp\",\"attributes\":{\"friendly_name\":\"Outdoor Temp\"}},"
		"{\"entity_id\":\"media_player.airplay_den\",\"attributes\":{\"friendly_name\":\"Den\"}},"
		"{\"entity_id\":\"media_player.bedroom\",\"attributes\":{\"friendly_name\":\"Bedroom AirPlay\"}},"
		"{\"entity_id\":\"media_player.office\",\"attributes\":{\"source_list\":[\"TV\",\"AirPlay\"]}},"
		"{\"entity_id\":\"media_player.kitchen\",\"attributes\":{}}"
		"]";
	ha_entity_t entities[8];
	int count = ha_parse_media_players(json, entities, 8);

	assert(count == 5);
	assert(strcmp(entities[0].entity_id, "media_player.living_room") == 0);
	assert(strcmp(entities[0].name, "Living Room") == 0);
	assert(strcmp(entities[0].udn, "ha:media_player.living_room") == 0);
	assert(strcmp(entities[4].entity_id, "media_player.kitchen") == 0);
	assert(strcmp(entities[4].name, "media_player.kitchen") == 0);

	puts("PASS test_ha_entities");
	return 0;
}
