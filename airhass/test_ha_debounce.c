/*
 * test_ha_debounce.c — tiny check for the stopped-state debounce counter.
 */

#include <assert.h>
#include <stdio.h>

#include "src/ha_api.h"

int main(void) {
	int c = 0;

	/* fires exactly at the limit, then resets */
	assert(!ha_debounce_reached(&c, 3) && c == 1);
	assert(!ha_debounce_reached(&c, 3) && c == 2);
	assert(ha_debounce_reached(&c, 3) && c == 0);

	/* a reset by the caller (playing seen) restarts the count */
	assert(!ha_debounce_reached(&c, 3) && c == 1);
	c = 0;
	assert(!ha_debounce_reached(&c, 3) && c == 1);

	puts("PASS test_ha_debounce");
	return 0;
}
