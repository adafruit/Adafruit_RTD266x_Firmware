# SPDX-License-Identifier: MIT
# GNU Make + SDCC; Linux/macOS or WSL. No proprietary compiler or libraries.
BOARD ?= uc586
PANEL ?= rgb800x480
APP ?= monitor
SPLASH ?= 1
TRACE ?= 0
SDCC ?= sdcc
HOST_CC ?= cc
OUT := build/$(BOARD)-$(PANEL)-$(APP)-splash$(SPLASH)
ifeq ($(TRACE),1)
OUT := $(OUT)-trace
endif

SOURCES := src/platform/io.c src/platform/mcs51.c src/platform/ddc.c \
           src/platform/diagnostics.c \
           src/rtd/edid.c src/rtd/video.c src/rtd/osd.c \
           boards/$(BOARD)/board.c panels/$(PANEL).c src/app/$(APP).c
OBJECTS := $(patsubst %.c,$(OUT)/%.rel,$(SOURCES))
HEADERS := $(wildcard include/rtd/*.h boards/$(BOARD)/*.h)
CFLAGS := -mmcs51 --std-c11 --model-large --stack-auto --no-xinit-opt \
          -Iinclude -Iboards/$(BOARD) -DRTD_SPLASH=$(SPLASH) -DRTD_TRACE=$(TRACE)
LDFLAGS := --xram-loc 0xfb00 --xram-size 512 --code-size 65536

.PHONY: all firmware check
all: firmware
firmware: $(OUT)/firmware.bin
	python3 tests/firmware_test.py $(OUT)/firmware.bin $(OUT)/firmware.map

$(OUT)/%.rel: %.c $(HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(SDCC) $(CFLAGS) -c $< -o $@

$(OUT)/firmware.ihx: $(OBJECTS)
	$(SDCC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $@

$(OUT)/firmware.bin: $(OUT)/firmware.ihx
	makebin -s 65536 $< $@

check: firmware build/tests/edid_test build/tests/video_test
	build/tests/edid_test
	build/tests/video_test

build/tests/edid_test: tests/edid_test.c src/rtd/edid.c panels/$(PANEL).c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< src/rtd/edid.c panels/$(PANEL).c -o $@

build/tests/video_test: tests/video_test.c src/rtd/video.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< src/rtd/video.c -o $@
