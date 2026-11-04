#include "panasonic_ir.h"
#include "esphome/core/log.h"

namespace esphome::panasonic_ir {

ESPHOME_LOG_TAG(TAG, "panasonic_ir.protocol");

static const LogString *status_name(CommandStatus status) {
  switch (status) {
    case CommandStatus::COMMAND_STATUS_VALID_KNOWN:
      return LOG_STR("valid, known encoding");
    case CommandStatus::COMMAND_STATUS_VALID_UNKNOWN:
      return LOG_STR("valid, unknown fields/action");
    case CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM:
      return LOG_STR("INVALID checksum");
    default:
      return LOG_STR("incomplete/malformed (complete bytes only)");
  }
}

static const LogString *action_name(ShortAction action) {
  switch (action) {
    case ShortAction::SHORT_ACTION_CANCEL_ALL:
      return LOG_STR("ALL CANCEL");
    case ShortAction::SHORT_ACTION_POWERFUL_QUIET:
      return LOG_STR("POWERFUL/QUIET cycle");
    case ShortAction::SHORT_ACTION_NANOE_G:
      return LOG_STR("NANOE-G button (state unknown)");
    case ShortAction::SHORT_ACTION_ECONAVI_ON:
      return LOG_STR("ECONAVI ON");
    case ShortAction::SHORT_ACTION_ECONAVI_OFF:
      return LOG_STR("ECONAVI OFF");
    default:
      return LOG_STR("unknown short command for configured model");
  }
}

static const LogString *mode_name(uint8_t mode) {
  switch (static_cast<Mode>(mode)) {
    case Mode::AUTO:
      return LOG_STR("AUTO");
    case Mode::DRY:
      return LOG_STR("DRY");
    case Mode::COOL:
      return LOG_STR("COOL");
    case Mode::HEAT:
      return LOG_STR("HEAT");
    case Mode::FAN:
      return LOG_STR("FAN");
    default:
      return LOG_STR("unknown");
  }
}

static void log_time(const LogString *name, uint16_t minutes, bool timer_field = true) {
  if (timer_field && minutes == TIMER_TIME_SPECIAL) {
    ESP_LOGD(TAG, "  %s: special timer value 0x600 (not a time; see RKR ON1 observations)", LOG_STR_ARG(name));
  } else if (minutes < 24 * 60) {
    ESP_LOGD(TAG, "  %s: %02u:%02u (raw=%u min)", LOG_STR_ARG(name), minutes / 60, minutes % 60, minutes);
  } else {
    ESP_LOGD(TAG, "  %s: unknown time encoding 0x%03X", LOG_STR_ARG(name), minutes);
  }
}

void PanasonicIrClimate::log_command_(const Command &command, CommandStatus status) {
  char hex[format_hex_pretty_size(STATE_LENGTH)];
  ESP_LOGD(
      TAG, "RX %u bytes (%s): %s%s", static_cast<unsigned>(command.length),
      command.complete ? (command.length == SHORT_COMMAND_LENGTH ? LOG_STR_LITERAL("short") : LOG_STR_LITERAL("full"))
                       : LOG_STR_LITERAL("partial"),
      LOG_STR_ARG(status_name(status)),
      command.reconstructed_prefix ? LOG_STR_LITERAL("; constant first section reconstructed") : LOG_STR_LITERAL(""));
  ESP_LOGD(TAG, "  Bytes: %s", format_hex_pretty_to(hex, command.bytes, command.length, ' '));
  if (status == CommandStatus::COMMAND_STATUS_INCOMPLETE)
    return;
  const uint8_t *b = command.bytes;
  ESP_LOGD(TAG, "  Checksums: first=%02X expected=%02X; second=%02X expected=%02X", b[7], sum_bytes(b, 7),
           b[command.length - 1], sum_bytes(b + 8, command.length - 9));
  if (status == CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM)
    return;
  if (command.length == SHORT_COMMAND_LENGTH) {
    ESP_LOGD(TAG, "  Action: %s; payload=%02X %02X %02X", LOG_STR_ARG(action_name(short_action(command, this->model_))),
             b[12], b[13], b[14]);
    return;
  }
  const uint8_t mode = (b[13] >> 4) & 7;
  ESP_LOGD(TAG, "  Power%s=%u; mode=%s (%u); encoded target=%u C",
           this->model_ == Model::CKP ? LOG_STR_LITERAL(" toggle") : LOG_STR_LITERAL(""), b[13] & 1,
           LOG_STR_ARG(mode_name(mode)), mode, (b[14] >> 1) & 31);
  ESP_LOGD(TAG, "  Fan nibble=%u (3..7=min..max, 10=auto); vertical=%u (1..5=fixed, 15=auto)", b[16] >> 4, b[16] & 15);
  ESP_LOGD(
      TAG, "  Horizontal=%u (%s; 6=middle, 9..12=fixed, 13=auto)", b[17] & 15,
      supports_horizontal_vane(this->model_) ? LOG_STR_LITERAL("supported model") : LOG_STR_LITERAL("model-specific"));
  ESP_LOGD(TAG, "  Timer enable bits: ON=%u OFF=%u; unknown timer-adjacent flags: B19.3=%u B20.7=%u", (b[13] >> 1) & 1,
           (b[13] >> 2) & 1, (b[19] >> 3) & 1, b[20] >> 7);
  log_time(LOG_STR("ON timer"), b[18] | ((b[19] & 7) << 8));
  log_time(LOG_STR("OFF timer"), (b[19] >> 4) | ((b[20] & 0x7F) << 4));
  if (this->model_ == Model::RKR && has_rkr_ordinary_timer_fields(command)) {
    ESP_LOGD(TAG, "  Clock raw=0x000 (00:00); ordinary-command pattern: current remote clock not supplied");
    ESP_LOGD(TAG, "  RKR ON1 observation: with B13.3=1 and special time, ON enable retained an existing timer;");
    ESP_LOGD(TAG, "    clearing ON enable cancelled it; setting it did not activate an unprogrammed timer");
  } else {
    log_time(LOG_STR("Raw clock field (candidate current time; context-dependent)"), b[24] | ((b[25] & 7) << 8), false);
  }
  ESP_LOGD(TAG, "  B13.3=%u (RKR timer-policy observation); B25.6=%u B25.7=%u (ON/OFF slot 2 in timer context)",
           (b[13] >> 3) & 1, (b[25] >> 6) & 1, b[25] >> 7);
  if (this->model_ == Model::RKR) {
    if (b[23] == 0x80 && (b[13] & 8)) {
      ESP_LOGD(TAG, "  Timer slots: ON=%u OFF=%u", (b[25] & 0x40) ? 2 : 1, (b[25] & 0x80) ? 2 : 1);
      ESP_LOGD(TAG, "  RKR timer-command context: B23=80, B13.3=1 (observed SET/CANCEL; B23 bit meanings unknown)");
    }
    ESP_LOGD(TAG, "  MILD DRY observation: %s (B15=%02X)",
             mode == static_cast<uint8_t>(Mode::COOL) && b[15] == 0x7F   ? LOG_STR_LITERAL("ON encoding")
             : mode == static_cast<uint8_t>(Mode::COOL) && b[15] == 0x80 ? LOG_STR_LITERAL("OFF encoding")
                                                                         : LOG_STR_LITERAL("unknown in this context"),
             b[15]);
    if (b[23] == 0x89) {
      ESP_LOGD(TAG, "  iAUTO-X operating-frame encoding=%u",
               (b[13] & 1) && mode == static_cast<uint8_t>(Mode::AUTO) && (b[25] & 0x80));
    }
  }
  ESP_LOGD(TAG, "  Model/unknown fields: B12=%02X B13=%02X B14=%02X B15=%02X B17=%02X", b[12], b[13], b[14], b[15],
           b[17]);
  ESP_LOGD(TAG, "  Model/unknown fields: B21=%02X B22=%02X B23=%02X B25=%02X", b[21], b[22], b[23], b[25]);
}

bool PanasonicIrClimate::send_command_hex(const std::string &hex) {
  Command command;
  if (!parse_command_hex(StringRef(hex), command)) {
    ESP_LOGW(TAG, "Rejected Panasonic hex: expected 16/27 bytes or 8/19-byte second section, optional final checksum");
    return false;
  }
  char formatted[format_hex_pretty_size(STATE_LENGTH)];
  ESP_LOGI(TAG, "TX %u bytes (checksum calculated): %s", static_cast<unsigned>(command.length),
           format_hex_pretty_to(formatted, command.bytes, command.length, ' '));
  this->echo_length_ = command.length;
  memcpy(this->echo_, command.bytes + SECTION1_LENGTH, command.length - SECTION1_LENGTH);
  this->echo_sent_ms_ = millis();
  auto transmit = this->transmitter_->transmit();
  if (command.length == SHORT_COMMAND_LENGTH) {
    uint8_t short_frame[SHORT_COMMAND_LENGTH];
    memcpy(short_frame, command.bytes, sizeof(short_frame));
    encode_waveform(*transmit.get_data(), short_frame);
  } else {
    encode_waveform(*transmit.get_data(), command.bytes);
  }
  transmit.perform();
  return true;
}

}  // namespace esphome::panasonic_ir
