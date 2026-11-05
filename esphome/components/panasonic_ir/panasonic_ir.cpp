#include "panasonic_ir.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::panasonic_ir {

ESPHOME_LOG_TAG(TAG, "panasonic_ir.climate");

// How long after sending a frame an identical one is taken for its echo
static constexpr uint32_t ECHO_WINDOW_MS = 1000;

// In the order of the Model enum
PROGMEM_STRING_TABLE(ModelStrings, "DKE", "JKE", "LKE", "NKE", "CKP", "RKR", "PKE");

void PanasonicIrClimate::setup() {
  climate_ir::ClimateIR::setup();
  // The restored mode is the best guess of whether the unit is on
  this->ckp_is_on_ = this->mode != climate::CLIMATE_MODE_OFF;
  this->sync_vane_select_();
}

void PanasonicIrClimate::dump_config() {
  climate_ir::ClimateIR::dump_config();
  ESP_LOGCONFIG(TAG, "  Model: %s",
                LOG_STR_ARG(ModelStrings::get_log_str(static_cast<uint8_t>(this->model_), ModelStrings::LAST_INDEX)));
}

void PanasonicIrClimate::set_vane_position(size_t index) {
  if (index > VANE_V_LOWEST) {
    ESP_LOGW(TAG, "Vane position %u out of range", static_cast<unsigned>(index));
    return;
  }
  // A vane position takes the unit out of frost protection, as a swing change does
  this->preset = climate::CLIMATE_PRESET_NONE;
  // The select only offers 0 to 5
  if (index == 0) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else {
    this->vane_position_ = index;
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }
  this->transmit_state();
  this->publish_state();
}

uint8_t PanasonicIrClimate::vane_v_() const {
  if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL)
    return VANE_V_AUTO;
  if (this->vane_position_ != 0)
    return this->vane_position_;
  // Blow down to heat from the bottom of the room, and up to cool from the top
  return this->mode == climate::CLIMATE_MODE_HEAT ? VANE_V_LOWEST : VANE_V_HIGHEST;
}

void PanasonicIrClimate::sync_vane_select_() {
#ifdef USE_SELECT
  if (this->vane_select_ == nullptr)
    return;
  const uint8_t vane = this->vane_v_();
  const size_t index = vane == VANE_V_AUTO ? 0 : vane;
  if (this->vane_select_->active_index().value_or(SIZE_MAX) != index)
    this->vane_select_->publish_state(index);
#endif
}

void PanasonicIrClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->iauto_ = false;
  // The base class only knows about the regular fan modes
  if (call.has_custom_fan_mode()) {
    this->set_custom_fan_mode_(call.get_custom_fan_mode());
  } else if (call.get_fan_mode().has_value()) {
    this->clear_custom_fan_mode_();
  }

  // Frost protection is a mode of its own on the remote. Changing anything else takes the unit out of it
  const bool in_maintenance = this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_AWAY;
  if (call.get_preset().has_value()) {
    // Choosing frost protection turns the unit on. transmit_state() sets the rest
    if (*call.get_preset() == climate::CLIMATE_PRESET_AWAY)
      this->mode = climate::CLIMATE_MODE_HEAT;
  } else if (in_maintenance &&
             (call.get_mode().has_value() || call.get_target_temperature().has_value() ||
              call.get_fan_mode().has_value() || call.has_custom_fan_mode() || call.get_swing_mode().has_value())) {
    this->preset = climate::CLIMATE_PRESET_NONE;
  }
  climate_ir::ClimateIR::control(call);
}

uint8_t PanasonicIrClimate::fan_speed_() const {
  if (this->has_custom_fan_mode()) {
    const auto custom_fan_mode = this->get_custom_fan_mode();
    if (custom_fan_mode == FAN_MODE_MINIMUM)
      return FAN_SPEED_MIN;
    if (custom_fan_mode == FAN_MODE_MAXIMUM)
      return FAN_SPEED_MAX;
  }
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:
      return FAN_SPEED_LOW;
    case climate::CLIMATE_FAN_MEDIUM:
      return FAN_SPEED_MEDIUM;
    case climate::CLIMATE_FAN_HIGH:
      return FAN_SPEED_HIGH;
    default:
      return FAN_SPEED_AUTO;
  }
}

void PanasonicIrClimate::transmit_state() {
  // Frost protection is sent as the remote does, whatever else a call combined with the preset set, and the entity
  // shows what is sent
  const bool maintenance = this->mode != climate::CLIMATE_MODE_OFF &&
                           this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_AWAY;
  if (maintenance) {
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->set_custom_fan_mode_(FAN_MODE_MAXIMUM);
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  }

  FrameParams params;
  params.model = this->model_;

  bool power = true;
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      params.mode = Mode::AUTO;
      break;
    case climate::CLIMATE_MODE_DRY:
      params.mode = Mode::DRY;
      break;
    case climate::CLIMATE_MODE_COOL:
      params.mode = Mode::COOL;
      break;
    case climate::CLIMATE_MODE_HEAT:
      params.mode = Mode::HEAT;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      params.mode = Mode::FAN;
      break;
    case climate::CLIMATE_MODE_OFF:
    default:
      power = false;
      params.mode = this->last_mode_;
      break;
  }
  if (power)
    this->last_mode_ = params.mode;

  if (this->model_ == Model::CKP) {
    // No declarative power on CKP. The power bit toggles the unit, so only set it when the state changes
    params.power = (power != this->ckp_is_on_);
    this->ckp_is_on_ = power;
  } else {
    params.power = power;
  }

  params.temperature = maintenance
                           ? TEMP_MAINTENANCE
                           : static_cast<uint8_t>(lroundf(clamp<float>(this->target_temperature, TEMP_MIN, TEMP_MAX)));
  params.fan_speed = this->fan_speed_();
  params.vane_v = this->vane_v_();

  uint8_t state[STATE_LENGTH];
  encode_frame(params, state);
  if (this->model_ == Model::RKR) {
    this->iauto_ = this->iauto_ && this->mode == climate::CLIMATE_MODE_HEAT_COOL;
    if (this->iauto_)
      state[25] |= 0x80;
    if (this->mode == climate::CLIMATE_MODE_COOL && this->mild_dry_)
      state[15] = 0x7F;
    state[13] &= ~0x08;  // Preserve hardware timers on operating commands.
    state[26] = calc_checksum(state, STATE_LENGTH);
  }
  memcpy(this->echo_, state + SECTION1_LENGTH, SECTION2_LENGTH);
  this->echo_sent_ms_ = millis();

  auto transmit = this->transmitter_->transmit();
  encode_waveform(*transmit.get_data(), state);
  transmit.perform();
  this->sync_vane_select_();
}

void PanasonicIrClimate::send_short_(uint8_t first, uint8_t second) {
  if (this->model_ != Model::RKR)
    return;
  uint8_t frame[SHORT_COMMAND_LENGTH];
  encode_short_command({0x80, first, second}, frame);
  auto transmit = this->transmitter_->transmit();
  encode_waveform(*transmit.get_data(), frame);
  transmit.perform();
}

void PanasonicIrClimate::enable_iauto() {
  if (this->model_ != Model::RKR)
    return;
  this->iauto_ = true;
  this->mode = climate::CLIMATE_MODE_HEAT_COOL;
  this->preset = climate::CLIMATE_PRESET_NONE;
  this->target_temperature = 25;
  this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
  this->transmit_state();
  this->publish_state();
}

#ifdef USE_SWITCH
void PanasonicIrClimate::set_mild_dry(bool enabled) {
  if (this->model_ != Model::RKR || this->mode != climate::CLIMATE_MODE_COOL)
    return;
  this->mild_dry_ = enabled;
  this->transmit_state();
  if (this->mild_dry_switch_)
    this->mild_dry_switch_->publish_state(enabled);
  if (enabled && this->econavi_switch_)
    this->econavi_switch_->publish_state(false);
}

void PanasonicIrClimate::set_econavi(bool enabled) {
  if (this->model_ != Model::RKR || this->iauto_ || this->mode == climate::CLIMATE_MODE_FAN_ONLY ||
      (this->mode == climate::CLIMATE_MODE_COOL && this->mild_dry_))
    return;
  this->send_short_(enabled ? 0x68 : 0x52, enabled ? 0xF2 : 0x33);
  if (this->econavi_switch_)
    this->econavi_switch_->publish_state(enabled);
}
#endif

bool PanasonicIrClimate::on_receive(remote_base::RemoteReceiveData data) {
  uint8_t state[STATE_LENGTH];
  bool short_command = false;
  if (!read_state(data, state, this->model_ == Model::RKR ? &short_command : nullptr))
    return false;
  if (short_command) {
#ifdef USE_SWITCH
    const uint16_t action = encode_uint16(state[13], state[14]);
    if (this->econavi_switch_ && (action == 0x68F2 || action == 0x5233))
      this->econavi_switch_->publish_state(action == 0x68F2);
#endif
    return true;
  }

  FrameParams params;
  params.model = this->model_;
  if (!decode_frame(state, params)) {
    ESP_LOGD(TAG, "Received frame is not a valid Panasonic state");
    return false;
  }

  // A receiver near the transmitter hears our own frame. Applying it would flip the CKP power toggle back and store
  // the vane position derived from the mode as one the user chose.
  if (millis() - this->echo_sent_ms_ < ECHO_WINDOW_MS &&
      memcmp(state + SECTION1_LENGTH, this->echo_, SECTION2_LENGTH) == 0) {
    ESP_LOGD(TAG, "Ignoring the echo of our own frame");
    this->echo_sent_ms_ -= ECHO_WINDOW_MS;  // only the first echo
    return true;
  }

  if (this->model_ == Model::RKR && state[23] == 0x89) {
    this->iauto_ = params.power && params.mode == Mode::AUTO && (state[25] & 0x80);
    if (params.mode == Mode::COOL && (state[15] == 0x7F || state[15] == 0x80)) {
      this->mild_dry_ = state[15] == 0x7F;
#ifdef USE_SWITCH
      if (this->mild_dry_switch_)
        this->mild_dry_switch_->publish_state(this->mild_dry_);
      if (this->mild_dry_ && this->econavi_switch_)
        this->econavi_switch_->publish_state(false);
#endif
    }
  }
  this->apply_received_(params);
  return true;
}

void PanasonicIrClimate::apply_received_(const FrameParams &params) {
  bool power = params.power;
  if (this->model_ == Model::CKP) {
    // The power bit is only a toggle
    if (params.power)
      this->ckp_is_on_ = !this->ckp_is_on_;
    power = this->ckp_is_on_;
  }

  this->last_mode_ = params.mode;
  if (!power) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (params.mode) {
      case Mode::DRY:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      case Mode::COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case Mode::HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case Mode::FAN:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case Mode::AUTO:
      default:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
    }
  }

  // The frost protection frame holds 8 C or 10 C, which is not a set-point of the normal range. Keep the one we had
  const bool maintenance = params.temperature < TEMP_MIN;
  this->preset = maintenance ? climate::CLIMATE_PRESET_AWAY : climate::CLIMATE_PRESET_NONE;
  // Fan only mode always sends a fixed temperature, so it says nothing about the set-point either
  if (params.mode != Mode::FAN && !maintenance)
    this->target_temperature = params.temperature;

  switch (params.fan_speed) {
    case FAN_SPEED_MIN:
      this->set_custom_fan_mode_(FAN_MODE_MINIMUM);
      break;
    case FAN_SPEED_LOW:
      this->set_fan_mode_(climate::CLIMATE_FAN_LOW);
      break;
    case FAN_SPEED_MEDIUM:
      this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
      break;
    case FAN_SPEED_HIGH:
      this->set_fan_mode_(climate::CLIMATE_FAN_HIGH);
      break;
    case FAN_SPEED_MAX:
      this->set_custom_fan_mode_(FAN_MODE_MAXIMUM);
      break;
    case FAN_SPEED_AUTO:
    default:
      this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
      break;
  }

  // Any other vane value is not one the remote sends, so it leaves the vane as it was
  if (params.vane_v == VANE_V_AUTO) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else if (params.vane_v >= VANE_V_HIGHEST && params.vane_v <= VANE_V_LOWEST) {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->vane_position_ = params.vane_v;
  } else {
    ESP_LOGV(TAG, "Ignoring vane value %u", params.vane_v);
  }
  this->sync_vane_select_();

  this->publish_state();
}

}  // namespace esphome::panasonic_ir
