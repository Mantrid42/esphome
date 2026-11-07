#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome::panasonic_ir {

/*
Panasonic A/C state: 27 bytes, sent LSB first as two sections (8 bytes + 19 bytes).

Based on IRremoteESP8266 by Kristian Lauszus and David Conran (LGPL-2.1):
https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Panasonic.cpp
Cross-checked against the reverse engineering notes by Mathieu:
https://github.com/r45635/HVAC-IR-Control/blob/master/Protocol/Panasonic%20HVAC%20IR%20Protocol%20specification.pdf
https://www.instructables.com/Reverse-engineering-of-an-Air-Conditioning-control/
and captures from the remote of a CS-NE9PKE.

B0-B12: static 0x0220E004000000060220E00400
B13:    b0 power (CKP: toggle), b4-6 mode, other bits per model
B14:    b1-5 temperature
B16:    b0-3 vertical vane (1 highest to 5 lowest, 15 auto), b4-7 fan speed + 3
B17:    horizontal vane on DKE/RKR (6 middle, 9-12 fixed positions, 13 auto); otherwise per model
B21, B23, B25: per model
B26:    checksum, sum of B0-B25 plus 0xF4 (the first section sums to 0x10C, so it is the sum of B8-B25)
Stateless buttons (quiet/powerful, nanoe-G) send an 8 byte second section, which is ignored.
*/

static constexpr size_t STATE_LENGTH = 27;
static constexpr size_t SECTION1_LENGTH = 8;
static constexpr size_t SECTION2_LENGTH = STATE_LENGTH - SECTION1_LENGTH;
static constexpr uint8_t CHECKSUM_INIT = 0xF4;

// PKE is not an IRremoteESP8266 model: it is JKE with B13 b3 set, as sent by the remote of a CS-NE9PKE (CWA75C4410).
enum class Model : uint8_t { DKE, JKE, LKE, NKE, CKP, RKR, PKE };

enum class Mode : uint8_t { AUTO = 0, DRY = 2, COOL = 3, HEAT = 4, FAN = 6 };

// Raw fan speeds, as used by IRremoteESP8266. 0 = minimum, 4 = maximum.
static constexpr uint8_t FAN_SPEED_MIN = 0;
static constexpr uint8_t FAN_SPEED_LOW = 1;
static constexpr uint8_t FAN_SPEED_MEDIUM = 2;
static constexpr uint8_t FAN_SPEED_HIGH = 3;
static constexpr uint8_t FAN_SPEED_MAX = 4;
static constexpr uint8_t FAN_SPEED_AUTO = 7;
static constexpr uint8_t FAN_SPEED_OFFSET = 3;

static constexpr uint8_t VANE_V_HIGHEST = 0x1;
static constexpr uint8_t VANE_V_MIDDLE = 0x3;
static constexpr uint8_t VANE_V_LOWEST = 0x5;
static constexpr uint8_t VANE_V_AUTO = 0xF;

static constexpr uint8_t VANE_H_MIDDLE = 0x6;
static constexpr uint8_t VANE_H_FULL_LEFT = 0x9;
static constexpr uint8_t VANE_H_LEFT = 0xA;
static constexpr uint8_t VANE_H_RIGHT = 0xB;
static constexpr uint8_t VANE_H_FULL_RIGHT = 0xC;
static constexpr uint8_t VANE_H_AUTO = 0xD;

// In the same order as the horizontal vane select options.
static constexpr uint8_t HORIZONTAL_VANE_POSITIONS[] PROGMEM = {VANE_H_AUTO,   VANE_H_FULL_LEFT, VANE_H_LEFT,
                                                                VANE_H_MIDDLE, VANE_H_RIGHT,     VANE_H_FULL_RIGHT};

inline bool supports_horizontal_vane(Model model) { return model == Model::DKE || model == Model::RKR; }

inline bool valid_horizontal_vane(uint8_t position) {
  return position == VANE_H_AUTO || position == VANE_H_MIDDLE ||
         (position >= VANE_H_FULL_LEFT && position <= VANE_H_FULL_RIGHT);
}

// The remote's frost protection ("+8/10 C Heat") is a mode of its own that only offers 8 C and 10 C. Both are
// understood when receiving, 10 C is what we send.
static constexpr uint8_t TEMP_MAINTENANCE = 10;
static constexpr uint8_t TEMP_MIN_RECEIVE = 8;
static constexpr uint8_t TEMP_MIN = 16;       // Celsius
static constexpr uint8_t TEMP_MAX = 30;       // Celsius
static constexpr uint8_t TEMP_FAN_MODE = 27;  // Celsius. The remote always sends this in fan mode

/// Per model bytes. B17 is the default horizontal vane on DKE and RKR (middle, 0x06).
struct ModelBytes {
  uint8_t b13_or;
  uint8_t b17;
  uint8_t b21_or;
  uint8_t b23;
  uint8_t b25;
};

// In the order of the Model enum
static constexpr ModelBytes MODEL_BYTES[] PROGMEM = {
    {0x00, 0x06, 0x00, 0x01, 0x06},  // DKE
    {0x00, 0x00, 0x00, 0x81, 0x00},  // JKE
    {0x02, 0x06, 0x00, 0x81, 0x00},  // LKE
    {0x00, 0x06, 0x00, 0x81, 0x00},  // NKE
    {0x00, 0x00, 0x10, 0x01, 0x00},  // CKP
    {0x00, 0x06, 0x00, 0x89, 0x00},  // RKR
    {0x08, 0x00, 0x00, 0x81, 0x00},  // PKE
};

struct FrameParams {
  Model model{Model::JKE};
  bool power{false};  // For CKP this means "toggle power"
  Mode mode{Mode::AUTO};
  uint8_t temperature{25};
  uint8_t fan_speed{FAN_SPEED_AUTO};
  uint8_t vane_v{VANE_V_AUTO};
  uint8_t vane_h{VANE_H_MIDDLE};
};

/// The part of the state that doesn't depend on the settings. Built in code, so it doesn't take up RAM on an ESP8266
inline void fill_template(uint8_t *state) {
  memset(state, 0, STATE_LENGTH);
  state[0] = 0x02;
  state[1] = 0x20;
  state[2] = 0xE0;
  state[3] = 0x04;
  state[7] = 0x06;
  state[8] = 0x02;
  state[9] = 0x20;
  state[10] = 0xE0;
  state[11] = 0x04;
  state[15] = 0x80;
  state[19] = 0x0E;
  state[20] = 0xE0;
}

inline uint8_t calc_checksum(const uint8_t *state, size_t len) { return sum_bytes(state, len - 1, CHECKSUM_INIT); }

/// Encodes `p` as is: the temperature is not clamped, except that fan only mode always sends 27 C.
inline void encode_frame(const FrameParams &p, uint8_t *state) {
  fill_template(state);
  ModelBytes model;
  progmem_memcpy(&model, &MODEL_BYTES[static_cast<uint8_t>(p.model)], sizeof(model));
  state[13] = model.b13_or | (p.power ? 0x01 : 0x00) | ((static_cast<uint8_t>(p.mode) & 0x07) << 4);
  const uint8_t temp = p.mode == Mode::FAN ? TEMP_FAN_MODE : p.temperature;
  state[14] = (temp & 0x1F) << 1;
  state[16] = (p.vane_v & 0x0F) | (((p.fan_speed + FAN_SPEED_OFFSET) & 0x0F) << 4);
  state[17] = supports_horizontal_vane(p.model) && valid_horizontal_vane(p.vane_h) ? p.vane_h : model.b17;
  state[21] = model.b21_or;
  state[23] = model.b23;
  state[25] = model.b25;
  state[STATE_LENGTH - 1] = calc_checksum(state, STATE_LENGTH);
}

/// Decode a state. Returns false if the state doesn't look like a valid Panasonic A/C state.
inline bool decode_frame(const uint8_t *state, FrameParams &p) {
  // The first 13 bytes are static
  uint8_t expected[STATE_LENGTH];
  fill_template(expected);
  if (memcmp(state, expected, 13) != 0)
    return false;
  if (state[STATE_LENGTH - 1] != calc_checksum(state, STATE_LENGTH))
    return false;

  const uint8_t mode = (state[13] >> 4) & 0x07;
  switch (static_cast<Mode>(mode)) {
    case Mode::AUTO:
    case Mode::DRY:
    case Mode::COOL:
    case Mode::HEAT:
    case Mode::FAN:
      break;
    default:
      return false;
  }

  const uint8_t temp = (state[14] >> 1) & 0x1F;
  if (temp < TEMP_MIN_RECEIVE || temp > TEMP_MAX)
    return false;

  const uint8_t fan = static_cast<uint8_t>((state[16] >> 4) - FAN_SPEED_OFFSET);  // Wraps below the offset
  if (fan > FAN_SPEED_MAX && fan != FAN_SPEED_AUTO)
    return false;

  p.power = state[13] & 0x01;
  p.mode = static_cast<Mode>(mode);
  p.temperature = temp;
  p.fan_speed = fan;
  p.vane_v = state[16] & 0x0F;
  p.vane_h = supports_horizontal_vane(p.model) ? state[17] & 0x0F : VANE_H_MIDDLE;
  return true;
}

// IR timings [us]
static constexpr uint32_t CARRIER_FREQUENCY = 36700;
static constexpr uint32_t HEADER_MARK = 3456;
static constexpr uint32_t HEADER_SPACE = 1728;
static constexpr uint32_t BIT_MARK = 432;
static constexpr uint32_t BIT_ONE_SPACE = 1296;
static constexpr uint32_t BIT_ZERO_SPACE = 432;
static constexpr uint32_t SECTION_GAP = 10000;
static constexpr remote_base::BitTimings BIT_TIMINGS{
    .bit_mark = BIT_MARK, .one_space = BIT_ONE_SPACE, .zero_space = BIT_ZERO_SPACE};

static constexpr size_t SHORT_COMMAND_LENGTH = 16;
inline void encode_short_command(const uint8_t (&command)[3], uint8_t (&frame)[SHORT_COMMAND_LENGTH]) {
  memset(frame, 0, sizeof(frame));
  frame[0] = frame[8] = 0x02;
  frame[1] = frame[9] = 0x20;
  frame[2] = frame[10] = 0xE0;
  frame[3] = frame[11] = 0x04;
  frame[7] = 0x06;
  memcpy(frame + 12, command, sizeof(command));
  frame[15] = calc_checksum(frame, SHORT_COMMAND_LENGTH);
}

/// Write either supported frame length to the transmitter buffer; does not start transmission.
template<size_t N> inline void encode_waveform(remote_base::RemoteTransmitData &data, const uint8_t (&frame)[N]) {
  static_assert(N == STATE_LENGTH || N == SHORT_COMMAND_LENGTH);
  data.set_carrier_frequency(CARRIER_FREQUENCY);
  // Two headers and footer marks, 16 timings per byte, and the section gap.
  data.reserve(2 * 3 + N * 16 + 1);
  data.item(HEADER_MARK, HEADER_SPACE);
  data.item_bytes<BIT_TIMINGS>(frame, SECTION1_LENGTH);
  data.item(BIT_MARK, SECTION_GAP);
  data.item(HEADER_MARK, HEADER_SPACE);
  data.item_bytes<BIT_TIMINGS>(frame + SECTION1_LENGTH, N - SECTION1_LENGTH);
  data.mark(BIT_MARK);
}

/// A footer mark, followed by the end of the capture or a long space (idle time or section gap), never a bit.
inline bool peek_footer(const remote_base::RemoteReceiveData &data) {
  return data.peek_mark(BIT_MARK) && (!data.is_valid(1) || data.peek_space_at_least(3 * BIT_ONE_SPACE, 1));
}

/// Reads header + bytes + footer mark of a section of either length. Returns the number of bytes read, or 0.
inline size_t receive_section(remote_base::RemoteReceiveData &data, uint8_t *bytes) {
  if (!data.expect_item(HEADER_MARK, HEADER_SPACE) || !data.expect_bytes<BIT_TIMINGS>(bytes, SECTION1_LENGTH))
    return 0;
  size_t len = SECTION1_LENGTH;
  if (!peek_footer(data)) {
    if (!data.expect_bytes<BIT_TIMINGS>(bytes + SECTION1_LENGTH, SECTION2_LENGTH - SECTION1_LENGTH) ||
        !peek_footer(data))
      return 0;
    len = SECTION2_LENGTH;
  }
  data.advance();
  return len;
}

/// Reads a state from a received capture. Returns true if `state` holds a complete state (not yet validated).
///
/// The 10 ms gap between the sections is as long as the default idle time, so a capture holds the whole frame or
/// one section. The first section is constant and outside the checksum, so a lone second section is enough and a
/// lone first section is ignored.
inline bool read_state(remote_base::RemoteReceiveData &data, uint8_t *state, bool *short_command = nullptr) {
  uint8_t section[SECTION2_LENGTH];
  size_t len = receive_section(data, section);

  uint8_t expected[STATE_LENGTH];
  fill_template(expected);

  if (len == SECTION1_LENGTH && memcmp(section, expected, SECTION1_LENGTH) == 0) {
    // The gap and the second section follow in a full capture. Anything else is just the receiver's idle space
    if (!data.is_valid(1) || !data.expect_space(SECTION_GAP))
      return false;
    len = receive_section(data, section);
  }
  if (short_command && len == SECTION1_LENGTH && memcmp(section, expected + SECTION1_LENGTH, 4) == 0 &&
      section[4] == 0x80 && section[7] == sum_bytes(section, 7)) {
    *short_command = true;
    memcpy(state, expected, SECTION1_LENGTH);
    memcpy(state + SECTION1_LENGTH, section, len);
    return true;
  }
  if (len != SECTION2_LENGTH)
    return false;

  memcpy(state, expected, SECTION1_LENGTH);
  memcpy(state + SECTION1_LENGTH, section, SECTION2_LENGTH);
  return true;
}

}  // namespace esphome::panasonic_ir
