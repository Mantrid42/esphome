#pragma once

#include "panasonic_ac_frame.h"
#include "esphome/core/string_ref.h"

namespace esphome::panasonic_ir {

enum class CommandStatus : uint8_t {
  COMMAND_STATUS_UNRELATED,
  COMMAND_STATUS_INCOMPLETE,
  COMMAND_STATUS_INVALID_CHECKSUM,
  COMMAND_STATUS_VALID_KNOWN,
  COMMAND_STATUS_VALID_UNKNOWN,
};

enum class ShortAction : uint8_t {
  SHORT_ACTION_UNKNOWN,
  SHORT_ACTION_CANCEL_ALL,
  SHORT_ACTION_POWERFUL_QUIET,
  SHORT_ACTION_NANOE_G,
  SHORT_ACTION_ECONAVI_ON,
  SHORT_ACTION_ECONAVI_OFF,
};

struct Command {
  uint8_t bytes[STATE_LENGTH]{};
  size_t length{0};
  bool reconstructed_prefix{false};
  bool complete{false};
};

inline bool has_panasonic_prefix(const uint8_t *bytes, size_t length) {
  return length >= 4 && bytes[0] == 0x02 && bytes[1] == 0x20 && bytes[2] == 0xE0 && bytes[3] == 0x04;
}

inline bool has_first_section(const uint8_t *bytes, size_t length) {
  return has_panasonic_prefix(bytes, length) && length >= SECTION1_LENGTH && bytes[4] == 0 && bytes[5] == 0 &&
         bytes[6] == 0 && bytes[7] == 6;
}

inline ShortAction short_action(const Command &command, Model model) {
  // Identifiers from this project's RKR captures, not a cross-model action catalogue.
  if (model != Model::RKR || command.length != SHORT_COMMAND_LENGTH || command.bytes[12] != 0x80)
    return ShortAction::SHORT_ACTION_UNKNOWN;
  const uint16_t action = encode_uint16(command.bytes[13], command.bytes[14]);
  switch (action) {
    case 0xB632:
      return ShortAction::SHORT_ACTION_CANCEL_ALL;
    case 0x9C32:
      return ShortAction::SHORT_ACTION_POWERFUL_QUIET;
    case 0x6133:
      return ShortAction::SHORT_ACTION_NANOE_G;
    case 0x68F2:
      return ShortAction::SHORT_ACTION_ECONAVI_ON;
    case 0x5233:
      return ShortAction::SHORT_ACTION_ECONAVI_OFF;
    default:
      return ShortAction::SHORT_ACTION_UNKNOWN;
  }
}

inline CommandStatus classify_command(const Command &command, Model model) {
  if (!has_panasonic_prefix(command.bytes, command.length))
    return CommandStatus::COMMAND_STATUS_UNRELATED;
  if (!command.complete || (command.length != STATE_LENGTH && command.length != SHORT_COMMAND_LENGTH) ||
      !has_panasonic_prefix(command.bytes + SECTION1_LENGTH, command.length - SECTION1_LENGTH))
    return CommandStatus::COMMAND_STATUS_INCOMPLETE;
  if (sum_bytes(command.bytes, SECTION1_LENGTH - 1) != command.bytes[7] ||
      sum_bytes(command.bytes + SECTION1_LENGTH, command.length - SECTION1_LENGTH - 1) !=
          command.bytes[command.length - 1])
    return CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM;
  if (!has_first_section(command.bytes, command.length))
    return CommandStatus::COMMAND_STATUS_VALID_UNKNOWN;
  if (command.length == SHORT_COMMAND_LENGTH)
    return short_action(command, model) == ShortAction::SHORT_ACTION_UNKNOWN
               ? CommandStatus::COMMAND_STATUS_VALID_UNKNOWN
               : CommandStatus::COMMAND_STATUS_VALID_KNOWN;
  FrameParams params;
  params.model = model;
  params.apply_timers = command.bytes[13] & 8;
  if (!decode_frame(command.bytes, params))
    return CommandStatus::COMMAND_STATUS_VALID_UNKNOWN;
  uint8_t expected[STATE_LENGTH];
  encode_frame(params, expected);
  // Only claim a fully known encoding for the exact operating-state template we generate.
  return memcmp(expected, command.bytes, STATE_LENGTH) == 0 ? CommandStatus::COMMAND_STATUS_VALID_KNOWN
                                                            : CommandStatus::COMMAND_STATUS_VALID_UNKNOWN;
}

/// Keep complete bytes from malformed sections for diagnostics. Never round a partial byte up.
inline bool read_command_section(remote_base::RemoteReceiveData &data, uint8_t *bytes, size_t capacity,
                                 size_t &length) {
  length = 0;
  if (!data.expect_item(HEADER_MARK, HEADER_SPACE))
    return false;
  while (length < capacity) {
    if (peek_footer(data)) {
      data.advance();
      return true;
    }
    if (!data.expect_bytes<BIT_TIMINGS>(bytes + length, 1))
      return false;
    length++;
  }
  if (!peek_footer(data))
    return false;
  data.advance();
  return true;
}

/// Full captures retain both sections. Split second sections explicitly reconstruct the constant prefix.
inline Command read_command(remote_base::RemoteReceiveData &data) {
  Command command;
  uint8_t section[SECTION2_LENGTH]{};
  size_t length;
  bool complete = read_command_section(data, section, sizeof(section), length);
  if (!has_panasonic_prefix(section, length))
    return command;
  memcpy(command.bytes, section, length);
  command.length = length;
  const bool first = length == SECTION1_LENGTH && section[4] == 0 && section[5] == 0 && section[6] == 0;
  if (complete && first && data.is_valid(1) && data.expect_space(SECTION_GAP)) {
    size_t second_length;
    complete = read_command_section(data, command.bytes + SECTION1_LENGTH, SECTION2_LENGTH, second_length);
    command.length += second_length;
    command.complete = complete && (second_length == SECTION1_LENGTH || second_length == SECTION2_LENGTH);
  } else if (complete && !first && (length == SECTION1_LENGTH || length == SECTION2_LENGTH)) {
    fill_template(command.bytes);
    memcpy(command.bytes + SECTION1_LENGTH, section, length);
    command.length = SECTION1_LENGTH + length;
    command.reconstructed_prefix = true;
    command.complete = true;
  }
  // At most one trailing idle space is part of the capture. Extra pulses must not become valid state.
  if (command.complete && data.is_valid()) {
    if (!data.peek_space_at_least(3 * BIT_ONE_SPACE))
      command.complete = false;
    data.advance();
    if (data.is_valid())
      command.complete = false;
  }
  return command;
}

/// Accept complete frames or second sections, optionally without the final checksum. Always regenerate it.
inline bool parse_command_hex(StringRef input, Command &command) {
  command = {};
  int high = -1;
  for (const char c : input) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (high >= 0)
        return false;
      continue;
    }
    const int digit = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
    if (digit < 0)
      return false;
    if (high < 0) {
      high = digit;
    } else {
      if (command.length == STATE_LENGTH)
        return false;
      command.bytes[command.length++] = (high << 4) | digit;
      high = -1;
    }
  }
  if (high >= 0)
    return false;
  size_t length = command.length;
  const bool second_only = length == 7 || length == 8 || length == 18 || length == 19;
  if (second_only) {
    memmove(command.bytes + SECTION1_LENGTH, command.bytes, length);
    command.bytes[0] = 2;
    command.bytes[1] = 0x20;
    command.bytes[2] = 0xE0;
    command.bytes[3] = 4;
    command.bytes[4] = command.bytes[5] = command.bytes[6] = 0;
    command.bytes[7] = 6;
    length += SECTION1_LENGTH;
  }
  if (length == SHORT_COMMAND_LENGTH - 1 || length == STATE_LENGTH - 1)
    length++;
  if ((length != SHORT_COMMAND_LENGTH && length != STATE_LENGTH) || !has_first_section(command.bytes, length) ||
      !has_panasonic_prefix(command.bytes + SECTION1_LENGTH, length - SECTION1_LENGTH) ||
      command.bytes[12] != (length == SHORT_COMMAND_LENGTH ? 0x80 : 0x00))
    return false;
  command.length = length;
  command.bytes[length - 1] = calc_checksum(command.bytes, length);
  command.complete = true;
  return true;
}

}  // namespace esphome::panasonic_ir
