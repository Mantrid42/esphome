#include "panasonic_ir.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::panasonic_ir {

ESPHOME_LOG_TAG(TAG, "panasonic_ir.protocol");

#ifdef ESPHOME_LOG_HAS_DEBUG
PROGMEM_STRING_TABLE(ModeNames, "AUTO", "unknown", "DRY", "COOL", "HEAT", "unknown", "FAN");
PROGMEM_STRING_TABLE(FanNames, "minimum", "low", "medium", "high", "maximum", "unknown", "unknown", "auto");
PROGMEM_STRING_TABLE(VerticalVaneNames, "unknown", "highest", "high", "middle", "low", "lowest");
PROGMEM_STRING_TABLE(HorizontalVaneNames, "far left", "left", "right", "far right", "auto", "unknown");

static const LogString *horizontal_vane_name(const FrameParams &params) {
  if (params.model != Model::DKE && params.model != Model::RKR)
    return LOG_STR("n/a");
  const uint8_t position = params.vane_h & 0x0F;
  if (position == VANE_H_MIDDLE)
    return LOG_STR("middle");
  return HorizontalVaneNames::get_log_str(position - 9, HorizontalVaneNames::LAST_INDEX);
}

static const LogString *short_command_name(uint16_t action) {
  switch (action) {
    case 0x68F2:
      return LOG_STR("ECONAVI ON");
    case 0x5233:
      return LOG_STR("ECONAVI OFF");
    case 0x6133:
      return LOG_STR("NANOE-G button");
    case 0x9C32:
      return LOG_STR("POWERFUL/QUIET cycle");
    default:
      return LOG_STR("unknown short command");
  }
}
#endif

void PanasonicIrClimate::log_command_(const FrameParams &params, optional<bool> mild_dry, bool iauto) {
#ifdef ESPHOME_LOG_HAS_DEBUG
  ESP_LOGD(
      TAG, "RX: power%s=%s mode=%s target=%u C fan=%s vane V=%s H=%s%s%s",
      params.model == Model::CKP ? LOG_STR_LITERAL(" toggle") : LOG_STR_LITERAL(""),
      params.power ? LOG_STR_LITERAL("ON") : LOG_STR_LITERAL("OFF"),
      LOG_STR_ARG(ModeNames::get_log_str(static_cast<uint8_t>(params.mode), 1)), params.temperature,
      LOG_STR_ARG(FanNames::get_log_str(params.fan_speed, 5)),
      LOG_STR_ARG(params.vane_v == VANE_V_AUTO ? LOG_STR("auto") : VerticalVaneNames::get_log_str(params.vane_v, 0)),
      LOG_STR_ARG(horizontal_vane_name(params)),
      !mild_dry.has_value() ? LOG_STR_LITERAL("")
      : *mild_dry           ? LOG_STR_LITERAL(" MILD DRY=ON")
                            : LOG_STR_LITERAL(" MILD DRY=OFF"),
      iauto ? LOG_STR_LITERAL(" iAUTO-X=ON") : LOG_STR_LITERAL(""));
#endif
}

void PanasonicIrClimate::log_short_command_(uint16_t action, bool echo) {
  ESP_LOGD(TAG, "%s: %s", echo ? LOG_STR_LITERAL("ECHO") : LOG_STR_LITERAL("RX"),
           LOG_STR_ARG(short_command_name(action)));
}

void PanasonicIrClimate::log_frame_(const LogString *direction, const uint8_t *state, size_t length) {
  ESP_LOGV_HEX(TAG, state, length, STATE_LENGTH, "%s %u bytes (checksums %02X/%02X, expected %02X/%02X): %s",
               LOG_STR_ARG(direction), static_cast<unsigned>(length), state[SECTION1_LENGTH - 1], state[length - 1],
               sum_bytes(state, SECTION1_LENGTH - 1), sum_bytes(state + SECTION1_LENGTH, length - SECTION1_LENGTH - 1));
}

void PanasonicIrClimate::log_tx_(const uint8_t *state, size_t length) {
  ESP_LOGD(TAG, "TX: %u-byte command", static_cast<unsigned>(length));
  log_frame_(LOG_STR("TX"), state, length);
}

}  // namespace esphome::panasonic_ir
