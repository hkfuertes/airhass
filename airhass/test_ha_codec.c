/*
 * test_ha_codec.c — tiny check for codec -> extension/content-type mapping.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/ha_api.h"

int main(void) {
	assert(strcmp(ha_codec_extension("flac"), "flac") == 0);
	assert(strcmp(ha_codec_content_type("flac"), "audio/flac") == 0);

	assert(strcmp(ha_codec_extension("mp3:320"), "mp3") == 0);
	assert(strcmp(ha_codec_content_type("mp3:320"), "audio/mpeg") == 0);

	assert(strcmp(ha_codec_extension("aac:256"), "aac") == 0);
	assert(strcmp(ha_codec_content_type("aac:256"), "audio/aac") == 0);

	assert(strcmp(ha_codec_extension("wav"), "wav") == 0);
	assert(strcmp(ha_codec_content_type("wav"), "audio/wav") == 0);

	assert(strcmp(ha_codec_extension(NULL), "flac") == 0);
	assert(strcmp(ha_codec_content_type(NULL), "audio/flac") == 0);

	puts("PASS test_ha_codec");
	return 0;
}
