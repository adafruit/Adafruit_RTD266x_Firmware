// SPDX-License-Identifier: MIT
#include "rtd/ddcci.h"
#include "rtd/io.h"
#include "rtd/platform.h"
#include "rtd/audio.h"

/* RTD2660 register manual pp.293-297: separate sub-address latch plus a
 * shared 16-byte RX/TX FIFO. Short complete replies fit without interrupts
 * or clock stretching. DDC-CI uses ordinary I2C; EDID and ISP stay separate.
 */
enum {
  SLAVE = 0x23,
  SUB_ADDRESS = 0x24,
  INPUT = 0x25,
  OUTPUT = 0x26,
  STATUS = 0x27,
  IRQ_ENABLE = 0x28,
  FIFO_STATUS = 0x29,
  FIFO_CONTROL = 0x2a,
  CHANNEL = 0x2b,
  STOP_RECEIVED = 0x10,
  SUB_RECEIVED = 0x02,
  FIFO_EMPTY = 0x02,
  FIFO_OVERFLOW = 0x20,
  FIFO_HOST = 0x80,
  FIFO_MCU = 0xa0,
  FIFO_RESET = 0x40
};

#ifdef __SDCC_mcs51
#define DDCCI_CODE __code
#else
#define DDCCI_CODE
#endif

static const DDCCI_CODE char capabilities[] =
    "(prot(monitor)type(LCD)model(Adafruit_RTD266x)"
    "cmds(01 03 F3)vcp(12 "
#if RTD_AUDIO_VOLUME
    "62 "
#endif
    "8D D6 DF E0 E1 E2 E3 E4 E5 E6 E7 E8 EB)"
    "mccs_ver(2.2))";

static uint8_t packet[DDCCI_PACKET_BYTES];
static uint8_t transmitting;
static uint8_t initialized;
static uint32_t response_started;

static uint8_t finish_response(uint8_t *response, uint8_t payload) {
  uint8_t checksum = 0x50, i;
  response[0] = 0x6e;
  response[1] = 0x80 | payload;
  for (i = 0; i < payload + 2; ++i)
    checksum ^= response[i];
  response[payload + 2] = checksum;
  return payload + 3;
}

uint8_t ddcci_packet(const uint8_t *request, uint8_t count, uint8_t *response) {
  uint8_t i, command, code, checksum = 0x6e, length, found;
  uint16_t maximum = 0, value = 0, offset;
  if (count < 3 || count > DDCCI_PACKET_BYTES || request[0] != 0x51 ||
      !(request[1] & 0x80))
    return 0;
  length = request[1] & 0x7f;
  if (length != count - 3)
    return 0;
  for (i = 0; i < count; ++i)
    checksum ^= request[i];
  if (checksum || !length)
    return 0;
  command = request[2];
  if (command == 0x01 && length == 2) {
    code = request[3];
    found = control_get(code, &maximum, &value);
    if (!found)
      maximum = value = 0;
    response[2] = 0x02;
    response[3] = found ? 0 : 1;
    response[4] = code;
    response[5] = 0; /* Set-parameter VCP, not a momentary value. */
    response[6] = maximum >> 8;
    response[7] = (uint8_t)maximum;
    response[8] = value >> 8;
    response[9] = (uint8_t)value;
    return finish_response(response, 8);
  }
  if (command == 0x03 && length == 4) {
    code = request[3];
    value = ((uint16_t)request[4] << 8) | request[5];
    control_set(code, value); /* Standard Set VCP has no reply packet. */
    return 0;
  }
  if (command == 0xf3 && length == 3) {
    offset = ((uint16_t)request[3] << 8) | request[4];
    response[2] = 0xe3;
    response[3] = offset >> 8;
    response[4] = (uint8_t)offset;
    i = 0;
    while (i < 10 && offset < sizeof capabilities) {
      response[5 + i++] = capabilities[offset++];
    }
    return finish_response(response, 3 + i);
  }
  /* A valid unsupported command gets the standard null response. Malformed
   * known commands must never dispatch or turn into partial VCP writes.
   */
  if (command == 0x01 || command == 0x03 || command == 0xf3)
    return 0;
  return finish_response(response, 0);
}

static void receive_mode(void) {
  mcu_write(FIFO_CONTROL, FIFO_HOST | FIFO_RESET);
  mcu_write(FIFO_CONTROL, FIFO_HOST);
  mcu_write(STATUS, 0);         /* Status flags are write-zero-to-clear. */
  mcu_write(FIFO_STATUS, 0x40); /* NACK full RX, no forced SCL stretching. */
  transmitting = 0;
}

void ddcci_init(void) {
  mcu_write(IRQ_ENABLE, 0);
  mcu_update(CHANNEL, 3, 2); /* DDC2; crystal-clock SCL setup delay. */
  receive_mode();
  mcu_write(SLAVE, 0x6f); /* Address 0x37 in bits7:1, DDC2 selection bit0. */
  initialized = 1;
}

void ddcci_service(void) {
  uint8_t status, count, reply, i;
  if (!initialized)
    return;
  status = mcu_read(STATUS);
  if (transmitting) {
    /* The complete reply is already in hardware. Return FIFO ownership
     * after its STOP, or recover when a requester abandons its reply.
     */
    if ((status & STOP_RECEIVED) ||
        (uint32_t)(platform_millis() - response_started) >= 250) {
      receive_mode();
    }
    return;
  }
  if (!(status & STOP_RECEIVED))
    return;
  if (!(status & SUB_RECEIVED) || (mcu_read(FIFO_STATUS) & FIFO_OVERFLOW)) {
    receive_mode();
    return;
  }
  packet[0] = mcu_read(SUB_ADDRESS);
  count = 1;
  while (count < DDCCI_PACKET_BYTES && !(mcu_read(FIFO_STATUS) & FIFO_EMPTY)) {
    packet[count++] = mcu_read(INPUT);
  }
  if (!(mcu_read(FIFO_STATUS) & FIFO_EMPTY)) {
    receive_mode();
    return;
  }
  reply = ddcci_packet(packet, count, packet);
  receive_mode();
  if (!reply)
    return;
  mcu_write(FIFO_CONTROL, FIFO_MCU | FIFO_RESET);
  mcu_write(FIFO_CONTROL, FIFO_MCU);
  for (i = 0; i < reply; ++i)
    mcu_write(OUTPUT, packet[i]);
  response_started = platform_millis();
  transmitting = 1;
}
