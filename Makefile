# SPDX-License-Identifier: MIT
# GNU Make + SDCC; Linux/macOS or WSL. No proprietary compiler or libraries.
BOARD ?= uc586
PANEL ?= rgb800x480
APP ?= monitor
SPLASH ?= 1
SPLASH_BMP ?= assets/splash.bmp
NO_SIGNAL_BMP ?= assets/no-signal.bmp
TRACE ?= 0
MENU_PREVIEW ?= 0
SDCC ?= sdcc
HOST_CC ?= cc
OUT := build/$(BOARD)-$(PANEL)-$(APP)-splash$(SPLASH)
ifeq ($(TRACE),1)
OUT := $(OUT)-trace
endif
ifeq ($(MENU_PREVIEW),1)
OUT := $(OUT)-menu-preview
endif

SOURCES := src/platform/io.c src/platform/mcs51.c src/platform/ddc.c \
           src/platform/diagnostics.c src/platform/ddcci.c src/app/control.c \
           src/rtd/edid.c src/rtd/video.c src/rtd/osd.c src/rtd/audio.c \
           boards/$(BOARD)/board.c panels/$(PANEL).c src/app/$(APP).c
OBJECTS := $(patsubst %.c,$(OUT)/%.rel,$(SOURCES))
HEADERS := $(wildcard include/rtd/*.h boards/$(BOARD)/*.h)
BITMAP_HEADER := $(OUT)/generated/splash_bitmap.h
NO_SIGNAL_HEADER := $(OUT)/generated/no_signal_bitmap.h
OSD_TEST := $(OUT)/tests/osd_test
CFLAGS := -mmcs51 --std-c11 --model-large --stack-auto --no-xinit-opt \
          -Iinclude -Iboards/$(BOARD) -I$(OUT)/generated \
          -DRTD_SPLASH=$(SPLASH) -DRTD_TRACE=$(TRACE) -DRTD_MENU_PREVIEW=$(MENU_PREVIEW)
LDFLAGS := --xram-loc 0xfb00 --xram-size 512 --code-size 65536

.PHONY: all firmware check FORCE
all: firmware
firmware: $(OUT)/firmware.bin
	python3 tests/firmware_test.py $(OUT)/firmware.bin $(OUT)/firmware.map

# Recheck the selected path even when switching to an older BMP. The converter
# leaves identical output untouched, so unchanged artwork does not recompile.
$(BITMAP_HEADER): tools/bmp_to_header.py FORCE
	python3 tools/bmp_to_header.py "$(SPLASH_BMP)" "$@"

$(NO_SIGNAL_HEADER): tools/bmp_to_header.py FORCE
	python3 tools/bmp_to_header.py "$(NO_SIGNAL_BMP)" "$@" --symbol no_signal

FORCE:

$(OUT)/%.rel: %.c $(HEADERS) $(BITMAP_HEADER) $(NO_SIGNAL_HEADER) Makefile
	@mkdir -p $(dir $@)
	$(SDCC) $(CFLAGS) -c $< -o $@

$(OUT)/firmware.ihx: $(OBJECTS)
	$(SDCC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $@

$(OUT)/firmware.bin: $(OUT)/firmware.ihx
	makebin -s 65536 $< $@

check: firmware build/tests/edid_test build/tests/video_test build/tests/audio_test build/tests/ddcci_test build/tests/control_test build/tests/board_test $(OSD_TEST)
	python3 tests/bitmap_test.py
	build/tests/edid_test
	build/tests/video_test
	build/tests/audio_test
	build/tests/ddcci_test
	build/tests/control_test
	build/tests/board_test
	$(OSD_TEST)

build/tests/edid_test: tests/edid_test.c src/rtd/edid.c panels/$(PANEL).c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< src/rtd/edid.c panels/$(PANEL).c -o $@

build/tests/video_test: tests/video_test.c src/rtd/video.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< src/rtd/video.c -o $@

build/tests/audio_test: tests/audio_test.c src/rtd/audio.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< src/rtd/audio.c -o $@

build/tests/ddcci_test: tests/ddcci_test.c src/platform/ddcci.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude $< src/platform/ddcci.c -o $@

build/tests/control_test: tests/control_test.c src/app/control.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DRTD_SPLASH=$(SPLASH) -Iinclude -Iboards/$(BOARD) $< src/app/control.c -o $@

build/tests/board_test: tests/board_test.c boards/$(BOARD)/board.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< boards/$(BOARD)/board.c -o $@

$(OSD_TEST): tests/osd_test.c src/rtd/osd.c panels/$(PANEL).c $(HEADERS) $(BITMAP_HEADER) $(NO_SIGNAL_HEADER) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) -I$(OUT)/generated $< src/rtd/osd.c panels/$(PANEL).c -o $@
