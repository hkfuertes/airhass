# Local build/check helpers for the source bundled in the Home Assistant add-on.
ARCH := $(shell uname -m)
HOST ?= $(if $(filter Darwin,$(shell uname -s)),macos,linux)
PLATFORM ?= $(ARCH)
CC ?= cc
TEST_BIN_DIR := bin/$(HOST)/$(PLATFORM)
JANSSON := airhass/libjansson/targets/$(HOST)/$(PLATFORM)
TESTS := test_ha_codec test_ha_config test_ha_debounce test_ha_entities test_ha_play_media test_ha_reverse_control test_ha_volume
TEST_BINS := $(addprefix $(TEST_BIN_DIR)/,$(TESTS))

.PHONY: all clean test $(TESTS)

all:
	$(MAKE) -C airhass HOST=$(HOST) PLATFORM=$(PLATFORM) CC=$(CC)

test: $(TEST_BINS)
	@for test in $(TEST_BINS); do ./$$test; done

$(TESTS): %: $(TEST_BIN_DIR)/%
	./$<

$(TEST_BIN_DIR)/test_ha_%: airhass/test_ha_%.c airhass/src/ha_api.c
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) -std=gnu11 -Wall -I airhass/src -I $(JANSSON)/include $^ $(JANSSON)/libjansson.a -o $@

clean:
	$(MAKE) -C airhass HOST=$(HOST) PLATFORM=$(PLATFORM) CC=$(CC) clean
	rm -rf bin/
