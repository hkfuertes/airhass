# AirHass root Makefile — detects macOS arch and delegates to airhass/
# ponytail: plain make, no cmake/autotools needed

ARCH   := $(shell uname -m)
HOST   := macos
PLATFORM ?= $(ARCH)
CC ?= clang
TEST_BIN_DIR := bin/$(HOST)/$(PLATFORM)
JANSSON := airhass/libjansson/targets/$(HOST)/$(PLATFORM)

.PHONY: all clean test_ha_reverse_control test_ha_volume

all:
	$(MAKE) -C airhass HOST=$(HOST) PLATFORM=$(PLATFORM) CC=$(CC)

test_ha_reverse_control: $(TEST_BIN_DIR)/test_ha_reverse_control
	./$<

test_ha_volume: $(TEST_BIN_DIR)/test_ha_volume
	./$<

$(TEST_BIN_DIR)/test_ha_reverse_control: airhass/test_ha_reverse_control.c airhass/src/ha_api.c
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) -std=gnu11 -Wall -I airhass/src -I $(JANSSON)/include $^ $(JANSSON)/libjansson.a -o $@

$(TEST_BIN_DIR)/test_ha_volume: airhass/test_ha_volume.c airhass/src/ha_api.c
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) -std=gnu11 -Wall -I airhass/src -I $(JANSSON)/include $^ $(JANSSON)/libjansson.a -o $@

clean:
	$(MAKE) -C airhass HOST=$(HOST) PLATFORM=$(PLATFORM) CC=$(CC) clean
	rm -rf bin/
