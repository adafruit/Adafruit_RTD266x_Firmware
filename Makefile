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
SETTINGS ?= 1
AUDIO_VOLUME ?= 1
ASPECT_4_3 ?= 1
ASPECT_16_9 ?= 1
EEPROM_DIAGNOSTICS ?= 0
SDCC ?= sdcc
HOST_CC ?= cc
OUT := build/$(BOARD)-$(PANEL)-$(APP)-splash$(SPLASH)
ifeq ($(TRACE),1)
OUT := $(OUT)-trace
endif
ifeq ($(MENU_PREVIEW),1)
OUT := $(OUT)-menu-preview
endif
ifeq ($(SETTINGS),0)
OUT := $(OUT)-volatile
endif
ifeq ($(AUDIO_VOLUME),0)
OUT := $(OUT)-no-volume
endif
ifeq ($(ASPECT_4_3),0)
OUT := $(OUT)-no-aspect43
endif
ifeq ($(ASPECT_16_9),0)
OUT := $(OUT)-no-aspect169
endif
ifeq ($(EEPROM_DIAGNOSTICS),1)
OUT := $(OUT)-eeprom-diagnostics
endif

SOURCES := src/platform/io.c src/platform/mcs51.c src/platform/ddc.c \
           src/platform/diagnostics.c src/platform/ddcci.c src/app/control.c src/app/firmware_crc.c \
           src/rtd/edid.c src/rtd/video.c src/rtd/osd.c src/rtd/audio.c \
           boards/$(BOARD)/board.c panels/$(PANEL).c src/app/$(APP).c
ifneq ($(filter 1,$(SETTINGS) $(EEPROM_DIAGNOSTICS)),)
SOURCES += boards/$(BOARD)/eeprom.c
endif
ifeq ($(SETTINGS),1)
SOURCES += src/app/storage.c
endif
OBJECTS := $(patsubst %.c,$(OUT)/%.rel,$(SOURCES))
HEADERS := $(wildcard include/rtd/*.h boards/$(BOARD)/*.h)
BITMAP_HEADER := $(OUT)/generated/splash_bitmap.h
NO_SIGNAL_HEADER := $(OUT)/generated/no_signal_bitmap.h
VIDEO_TEST := $(OUT)/tests/video_test
OSD_TEST := $(OUT)/tests/osd_test
AUDIO_TEST := $(OUT)/tests/audio_test
DDCCI_TEST := $(OUT)/tests/ddcci_test
CONTROL_TEST := $(OUT)/tests/control_test
DEFINES := -DRTD_SPLASH=$(SPLASH) -DRTD_AUDIO_VOLUME=$(AUDIO_VOLUME) \
           -DRTD_SETTINGS=$(SETTINGS) -DRTD_ASPECT_4_3=$(ASPECT_4_3) -DRTD_ASPECT_16_9=$(ASPECT_16_9) -DRTD_EEPROM_DIAGNOSTICS=$(EEPROM_DIAGNOSTICS)
CFLAGS := -mmcs51 --std-c11 --model-large --stack-auto --no-xinit-opt \
          -Iinclude -Iboards/$(BOARD) -I$(OUT)/generated \
          $(DEFINES) -DRTD_TRACE=$(TRACE) -DRTD_MENU_PREVIEW=$(MENU_PREVIEW)
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

check: firmware build/tests/edid_test $(VIDEO_TEST) $(AUDIO_TEST) $(DDCCI_TEST) $(CONTROL_TEST) build/tests/board_test build/tests/eeprom_test build/tests/storage_test build/tests/firmware_crc_test $(OSD_TEST)
	python3 tests/bitmap_test.py
	build/tests/edid_test
	$(VIDEO_TEST)
	$(AUDIO_TEST)
	$(DDCCI_TEST)
	$(CONTROL_TEST)
	build/tests/board_test
	build/tests/eeprom_test
	build/tests/storage_test
	build/tests/firmware_crc_test
	$(OSD_TEST)

build/tests/edid_test: tests/edid_test.c src/rtd/edid.c panels/$(PANEL).c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< src/rtd/edid.c panels/$(PANEL).c -o $@

$(VIDEO_TEST): tests/video_test.c src/rtd/video.c $(HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(DEFINES) -Iinclude -Iboards/$(BOARD) $< src/rtd/video.c -o $@

$(AUDIO_TEST): tests/audio_test.c src/rtd/audio.c $(HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(DEFINES) -Iinclude -Iboards/$(BOARD) $< src/rtd/audio.c -o $@

$(DDCCI_TEST): tests/ddcci_test.c src/platform/ddcci.c $(HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(DEFINES) -Iinclude $< src/platform/ddcci.c -o $@

$(CONTROL_TEST): tests/control_test.c src/app/control.c src/app/firmware_crc.c $(HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(DEFINES) -Iinclude -Iboards/$(BOARD) $< src/app/control.c src/app/firmware_crc.c -o $@

build/tests/firmware_crc_test: tests/firmware_crc_test.c src/app/firmware_crc.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude $< src/app/firmware_crc.c -o $@

build/tests/board_test: tests/board_test.c boards/$(BOARD)/board.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Iboards/$(BOARD) $< boards/$(BOARD)/board.c -o $@

build/tests/eeprom_test: tests/eeprom_test.c boards/$(BOARD)/eeprom.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude $< boards/$(BOARD)/eeprom.c -o $@

build/tests/storage_test: tests/storage_test.c src/app/storage.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude $< src/app/storage.c -o $@

$(OSD_TEST): tests/osd_test.c src/rtd/osd.c panels/$(PANEL).c $(HEADERS) $(BITMAP_HEADER) $(NO_SIGNAL_HEADER) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -DRTD_MENU_PREVIEW=1 -Iinclude -Iboards/$(BOARD) -I$(OUT)/generated $< src/rtd/osd.c panels/$(PANEL).c -o $@
