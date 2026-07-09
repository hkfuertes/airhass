# AirHass root Makefile — detects macOS arch and delegates to airhass/
# ponytail: plain make, no cmake/autotools needed

ARCH   := $(shell uname -m)
HOST   := macos
PLATFORM ?= $(ARCH)

.PHONY: all clean

all:
	$(MAKE) -C airhass HOST=$(HOST) PLATFORM=$(PLATFORM) CC=clang

clean:
	$(MAKE) -C airhass HOST=$(HOST) PLATFORM=$(PLATFORM) CC=clang clean
	rm -rf bin/
