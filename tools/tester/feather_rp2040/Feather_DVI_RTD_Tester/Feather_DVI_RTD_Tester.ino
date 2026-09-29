// SPDX-FileCopyrightText: 2026 Limor Fried for Adafruit Industries
// SPDX-License-Identifier: MIT
// USB-controlled HDMI/DDC engineering tester. See host.py for the host CLI.
// The display needs its own power. HDMI DDC uses GPIO2/3 through the Feather's
// level shifter. Color video has a pixel-doubled RGB565 framebuffer.

#include <PicoDVI.h>
#include <Wire.h>
#include <errno.h>
#include <hardware/structs/watchdog.h>
#include <hardware/watchdog.h>
#include <new>
#include "RTD266xISP.h"

const uint32_t MODE_COOKIE = 0x44564931;
const uint16_t SERIAL_LINE_BYTES = 1024;
// Provisional panel timing: 31.5 MHz, 1000 x 525. Pixels remain doubled RGB565.
const struct dvi_timing panelTiming = {
    false, 112, 48, 40, 800, false, 13, 3, 29, 480, 315000};
class PanelVideo : public DVIGFX16 {
public:
  PanelVideo() : DVIGFX16(DVI_RES_400x240p60, adafruit_feather_dvi_cfg) {
    dvi0.timing = &panelTiming;
  }
};
DVIGFX16 *display = nullptr;
// Match the zero-initialized global storage used by PicoDVI examples,
// while allowing the host to select a mode at runtime.
alignas(PanelVideo) uint8_t displayStorage[sizeof(PanelVideo)];
RTD266xISP flash;
uint16_t videoWidth = 0;
bool usePanelTiming = false;
char commandLine[SERIAL_LINE_BYTES];
uint16_t commandLength = 0;
bool lineOverflow = false;

void setup() {
  Serial.begin(115200);
  // Do not wait for USB: keep generating video when the host disconnects.
  delay(250);

  Serial.println("{\"event\":\"ready\",\"firmware\":\"FeatherDVI-RTD\"}");

  if (watchdog_hw->scratch[0] == MODE_COOKIE) {
    uint32_t savedMode = watchdog_hw->scratch[1];
    if (savedMode == 0 || savedMode == 640 || savedMode == 800) {
      videoWidth = savedMode;
    } else if (savedMode == 801) {
      videoWidth = 800;
      usePanelTiming = true;
    }
  }
  // A reset always returns to USB/DDC-only operation unless the host has
  // explicitly requested a video mode. Recover if video startup stalls.
  watchdog_hw->scratch[0] = 0;
  if (videoWidth) {
    watchdog_enable(8000, true);
  }
  if (usePanelTiming) {
    display = new (displayStorage) PanelVideo();
  } else if (videoWidth == 640) {
    display = new (displayStorage) DVIGFX16(DVI_RES_320x240p60, adafruit_feather_dvi_cfg);
  } else if (videoWidth == 800) {
    display = new (displayStorage) DVIGFX16(DVI_RES_400x240p60, adafruit_feather_dvi_cfg);
  }
  watchdog_disable();
  if (display && !display->begin()) {
    Serial.println("{\"event\":\"error\",\"error\":\"Video allocation failed\"}");
    while (true) {
      delay(1000);
    }
  }
  Wire.setSDA(2);
  Wire.setSCL(3);
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeout(100);
  drawPattern("bars");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      commandLine[commandLength] = 0;
      if (lineOverflow) {
        printError("Command is too long");
      } else if (commandLength) {
        processCommand(commandLine);
      }
      commandLength = 0;
      lineOverflow = false;
    } else if (commandLength < SERIAL_LINE_BYTES - 1) {
      commandLine[commandLength++] = c;
    } else {
      lineOverflow = true;
    }
  }
  delay(1);
}

void printError(const char *message) {
  Serial.print("{\"ok\":false,\"error\":\"");
  Serial.print(message);
  Serial.println("\"}");
}

void printResult(bool ok) {
  if (ok) {
    Serial.println("{\"ok\":true}");
  } else {
    printError(flash.error());
  }
}

void printHex(const uint8_t *data, size_t length) {
  const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < length; i++) {
    Serial.write(hex[data[i] / 16]);
    Serial.write(hex[data[i] % 16]);
  }
}

bool number(const char *text, uint32_t &value) {
  if (!text || !*text || *text == '-') {
    return false;
  }
  char *end;
  errno = 0;
  unsigned long parsed = strtoul(text, &end, 0);
  if (errno || *end) {
    return false;
  }
  value = parsed;
  return true;
}

bool hexBytes(const char *text, uint8_t *data, size_t &length) {
  if (!text) {
    return false;
  }
  size_t chars = strlen(text);
  if (!chars || chars % 2 || chars > 512) {
    return false;
  }
  length = chars / 2;
  for (size_t i = 0; i < length; i++) {
    char pair[3] = {text[2 * i], text[2 * i + 1], 0};
    if (!isxdigit(pair[0]) || !isxdigit(pair[1])) {
      return false;
    }
    data[i] = strtoul(pair, nullptr, 16);
  }
  return true;
}

bool readEdid(uint8_t block, uint8_t *data) {
  bool ok = true;
  if (block >= 2) {
    Wire.beginTransmission(0x30);
    Wire.write(block / 2);
    ok = Wire.endTransmission() == 0;
  }
  for (uint16_t offset = 0; ok && offset < 128; offset += 32) {
    Wire.beginTransmission(0x50);
    Wire.write((uint8_t)((block % 2) * 128 + offset));
    if (Wire.endTransmission(false)) {
      ok = false;
      break;
    }
    if (Wire.requestFrom((uint8_t)0x50, (uint8_t)32) != 32) {
      ok = false;
      break;
    }
    for (uint8_t i = 0; i < 32; i++) {
      data[offset + i] = Wire.read();
    }
  }
  // Also restore the segment pointer after a short read or lost ACK.
  if (block >= 2) {
    Wire.beginTransmission(0x30);
    Wire.write((uint8_t)0);
    if (Wire.endTransmission()) {
      ok = false;
    }
  }
  return ok;
}

void processCommand(char *line) {
  char *cmd = strtok(line, " ");
  char *arg1 = strtok(nullptr, " ");
  char *arg2 = strtok(nullptr, " ");
  if (strtok(nullptr, " ")) {
    printError("Too many arguments");
    return;
  }
  uint32_t address = 0;
  uint32_t length = 0;
  uint8_t data[256];
  if (!strcmp(cmd, "hello")) {
    Serial.print("{\"ok\":true,\"firmware\":\"FeatherDVI-RTD\",\"protocol\":1,\"video\":\"");
    if (display) {
      Serial.print(videoWidth);
      Serial.print("x480@60\",\"framebuffer\":\"");
      Serial.print(display->width());
      Serial.print("x240 RGB565");
    } else {
      Serial.print("off\",\"framebuffer\":\"none");
    }
    Serial.print("\",\"panel_timing\":");
    Serial.print(usePanelTiming ? "true" : "false");
    Serial.print(",\"isp_active\":");
    // Recover the actual RTD state even if the Feather itself has rebooted.
    // An absent or unresponsive RTD is unknown, never a false running claim.
    bool ispActive;
    if (flash.readISPState(ispActive)) {
      Serial.print(ispActive ? "true" : "false");
    } else {
      Serial.print("null");
    }
    Serial.println("}");
  } else if (!strcmp(cmd, "scan")) {
    Serial.print("{\"ok\":true,\"addresses\":[");
    bool first = true;
    for (uint8_t i = 1; i < 127; i++) {
      Wire.beginTransmission(i);
      if (Wire.endTransmission() == 0) {
        if (!first) {
          Serial.print(',');
        }
        Serial.print(i);
        first = false;
      }
    }
    Serial.println("]}");
  } else if (!strcmp(cmd, "edid")) {
    if ((arg1 && !number(arg1, address)) || address > 7) {
      printError("EDID block must be 0 through 7");
    } else if (!readEdid(address, data)) {
      printError("EDID read failed");
    } else {
      Serial.print("{\"ok\":true,\"block\":");
      Serial.print(address);
      Serial.print(",\"data\":\"");
      printHex(data, 128);
      Serial.println("\"}");
    }
  } else if (!strcmp(cmd, "ddc-config")) {
    RTD266xISP::DDCConfig config;
    if (arg1 || arg2) {
      printError("DDC configuration takes no arguments");
    } else if (!flash.readDDCConfig(config)) {
      printError(flash.error());
    } else {
      Serial.print("{\"ok\":true,\"registers\":{");
      for (size_t i = 0; i < RTD266xISP::DDC_REGISTER_COUNT; i++) {
        if (i) {
          Serial.print(',');
        }
        Serial.print('"');
        Serial.print(RTD266xISP::DDC_REGISTERS[i].name);
        Serial.print("\":\"");
        printHex(&config.values[i], 1);
        Serial.print('"');
      }
      Serial.print("},\"channel_access\":{\"FFEC\":\"");
      printHex(&config.channelAccess[0], 1);
      Serial.print("\",\"FFED\":\"");
      printHex(&config.channelAccess[1], 1);
      Serial.println("\"}}");
    }
  } else if (!strcmp(cmd, "isp")) {
    uint8_t status;
    if (!flash.enter() || !flash.readStatus(status)) {
      printError(flash.error());
    } else {
      Serial.print("{\"ok\":true,\"jedec\":\"");
      Serial.print(flash.jedecId(), HEX);
      Serial.print("\",\"size\":");
      Serial.print(flash.flashSize());
      Serial.print(",\"status\":");
      Serial.print(status);
      Serial.println("}");
    }
  } else if (!strcmp(cmd, "read")) {
    if (!number(arg1, address) || !number(arg2, length) || !length || length > 256) {
      printError("Read requires an address and length 1 through 256");
    } else if (!flash.read(address, data, length)) {
      printError(flash.error());
    } else {
      Serial.print("{\"ok\":true,\"address\":");
      Serial.print(address);
      Serial.print(",\"data\":\"");
      printHex(data, length);
      Serial.println("\"}");
    }
  } else if (!strcmp(cmd, "arm")) {
    if (!arg1 || strlen(arg1) != 6 || strspn(arg1, "0123456789abcdefABCDEF") != 6) {
      printError("Arm requires the six-digit flash JEDEC ID");
    } else {
      printResult(flash.arm(strtoul(arg1, nullptr, 16)));
    }
  } else if (!strcmp(cmd, "unlock")) {
    printResult(flash.unlock());
  } else if (!strcmp(cmd, "restore-protection")) {
    if (!number(arg1, address) || address > 255) {
      printError("restore-protection requires a recorded status byte");
    } else {
      printResult(flash.restoreProtection((uint8_t)address));
    }
  } else if (!strcmp(cmd, "erase")) {
    if (!number(arg1, address)) {
      printError("Erase requires a sector address");
    } else {
      printResult(flash.eraseSector(address));
    }
  } else if (!strcmp(cmd, "page")) {
    size_t bytes = 0;
    if (!number(arg1, address) || !hexBytes(arg2, data, bytes)) {
      printError("Page requires an address and 1 through 256 hex bytes");
    } else {
      printResult(flash.programPage(address, data, bytes));
    }
  } else if (!strcmp(cmd, "finish")) {
    printResult(flash.finish());
  } else if (!strcmp(cmd, "reset")) {
    printResult(flash.reset());
  } else if (!strcmp(cmd, "reset-chip")) {
    printResult(flash.resetChip());
  } else if (!strcmp(cmd, "pattern")) {
    if (!display) {
      printError("Video is off; use mode 640, 800, or panel");
    } else if (!arg1 || !drawPattern(arg1)) {
      printError("Unknown pattern");
    } else {
      Serial.println("{\"ok\":true}");
    }
  } else if (!strcmp(cmd, "mode")) {
    if (flash.active()) {
      printError("Finish the ISP session before changing video mode");
    } else if (!arg1 || (strcmp(arg1, "640") && strcmp(arg1, "800") &&
                        strcmp(arg1, "panel") && strcmp(arg1, "off"))) {
      printError("Mode must be 640, 800, panel, or off");
    } else {
      watchdog_hw->scratch[0] = MODE_COOKIE;
      if (!strcmp(arg1, "panel")) {
        watchdog_hw->scratch[1] = 801;
      } else {
        watchdog_hw->scratch[1] = atoi(arg1);
      }
      Serial.println("{\"ok\":true,\"rebooting\":true}");
      Serial.flush();
      delay(100);
      rp2040.reboot();
    }
  } else {
    printError("Unknown command");
  }
}

bool drawPattern(const char *name) {
  if (!display) {
    return false;
  }
  int16_t w = display->width();
  int16_t h = display->height();
  const uint16_t colors[] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0};
  if (!strcmp(name, "bars")) {
    for (uint8_t i = 0; i < 8; i++) {
      display->fillRect(i * w / 8, 0, (i + 1) * w / 8 - i * w / 8, h, colors[i]);
    }
  } else if (!strcmp(name, "checker")) {
    for (int16_t y = 0; y < h; y += 8) {
      for (int16_t x = 0; x < w; x += 8) {
        uint16_t color = 0;
        if ((x / 8 + y / 8) % 2) {
          color = 0xFFFF;
        }
        display->fillRect(x, y, 8, 8, color);
      }
    }
  } else if (!strcmp(name, "gray")) {
    for (int16_t x = 0; x < w; x++) {
      uint8_t shade = map(x, 0, w - 1, 0, 255);
      display->drawFastVLine(x, 0, h, display->color565(shade, shade, shade));
    }
  } else if (!strcmp(name, "grid")) {
    display->fillScreen(0);
    for (int16_t x = 0; x < w; x += 16) {
      display->drawFastVLine(x, 0, h, 0xFFFF);
    }
    for (int16_t y = 0; y < h; y += 16) {
      display->drawFastHLine(0, y, w, 0xFFFF);
    }
    display->drawRect(0, 0, w, h, 0xF800);
  } else if (!strcmp(name, "text")) {
    display->fillScreen(0);
    display->setTextColor(0xFFFF);
    display->setTextSize(2);
    display->setCursor(20, 50);
    display->println("Feather DVI / RTD");
    display->setCursor(20, 90);
    display->print(videoWidth);
    display->println(" x 480 @ 60 Hz");
    display->setCursor(20, 130);
    display->println("USB-controlled HIL");
  } else if (!strcmp(name, "red")) {
    display->fillScreen(0xF800);
  } else if (!strcmp(name, "green")) {
    display->fillScreen(0x07E0);
  } else if (!strcmp(name, "blue")) {
    display->fillScreen(0x001F);
  } else if (!strcmp(name, "white")) {
    display->fillScreen(0xFFFF);
  } else if (!strcmp(name, "black")) {
    display->fillScreen(0);
  } else {
    return false;
  }
  return true;
}
