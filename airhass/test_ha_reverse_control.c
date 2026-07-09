/*
 * test_ha_reverse_control.c — tiny check for HA state -> RAOP action mapping.
 */

#include <assert.h>
#include <stdio.h>

#include "src/ha_api.h"

int main(void) {
	assert(ha_state_to_raop_event("playing", HA_RAOP_NONE) == HA_RAOP_PLAY);
	assert(ha_state_to_raop_event("playing", HA_RAOP_PLAY) == HA_RAOP_NONE);

	assert(ha_state_to_raop_event("paused", HA_RAOP_PLAY) == HA_RAOP_PAUSE);
	assert(ha_state_to_raop_event("paused", HA_RAOP_NONE) == HA_RAOP_NONE);

	assert(ha_state_to_raop_event("idle", HA_RAOP_PLAY) == HA_RAOP_STOP);
	assert(ha_state_to_raop_event("off", HA_RAOP_NONE) == HA_RAOP_STOP);
	assert(ha_state_to_raop_event("unavailable", HA_RAOP_STOP) == HA_RAOP_NONE);

	puts("PASS test_ha_reverse_control");
	return 0;
}
