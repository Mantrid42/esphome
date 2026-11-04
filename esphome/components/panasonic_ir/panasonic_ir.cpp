#include "panasonic_rkr_timers.h"
#include "panasonic_ir.h"

#include <cmath>
#include <iterator>

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
  this->sync_horizontal_vane_select_();
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

void PanasonicIrClimate::set_horizontal_vane_position(size_t index) {
  if (!supports_horizontal_vane(this->model_) || index >= std::size(HORIZONTAL_VANE_POSITIONS)) {
    ESP_LOGW(TAG, "Horizontal vane position %u unsupported", static_cast<unsigned>(index));
    return;
  }
  this->preset = climate::CLIMATE_PRESET_NONE;
  this->horizontal_vane_position_ = progmem_read_byte(&HORIZONTAL_VANE_POSITIONS[index]);
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

void PanasonicIrClimate::sync_horizontal_vane_select_() {
#ifdef USE_SELECT
  if (this->horizontal_vane_select_ == nullptr || !supports_horizontal_vane(this->model_))
    return;
  for (size_t index = 0; index < std::size(HORIZONTAL_VANE_POSITIONS); index++) {
    if (this->horizontal_vane_position_ == progmem_read_byte(&HORIZONTAL_VANE_POSITIONS[index])) {
      if (this->horizontal_vane_select_->active_index().value_or(SIZE_MAX) != index)
        this->horizontal_vane_select_->publish_state(index);
      break;
    }
  }
#endif
}

void PanasonicIrClimate::control(const climate::ClimateCall &call) {
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

void PanasonicIrClimate::build_state_(uint8_t *state) {
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
  params.vane_h = this->horizontal_vane_position_;

  encode_frame(params, state);
  if (this->model_ == Model::RKR) {
    if (params.mode == Mode::COOL && this->mild_dry_)
      state[15] = 0x7F;
    if (this->mode != climate::CLIMATE_MODE_HEAT_COOL)
      this->iauto_ = false;
    if (this->iauto_)
      state[25] |= 0x80;
    state[26] = calc_checksum(state, STATE_LENGTH);
  }
}

#ifdef USE_PANASONIC_RKR_TIMERS
bool PanasonicIrClimate::api_set(StringRef power, StringRef hvac, int temperature, StringRef fan, StringRef vertical,
                                 StringRef horizontal, StringRef swing, StringRef preset, StringRef mild,
                                 StringRef iauto) {
  if (this->model_ != Model::RKR)
    return false;
  auto token = [](StringRef value, std::initializer_list<const char *> choices) {
    if (value.empty())
      return -1;
    int index = 0;
    for (auto choice : choices) {
      if (value == choice)
        return index;
      index++;
    }
    return -2;
  };
  int p = token(power, {"OFF", "ON"}), m = token(hvac, {"AUTO", "DRY", "COOL", "HEAT", "FAN_ONLY", "OFF", "HEAT_COOL"}),
      f = token(fan, {"AUTO", "MINIMUM", "LOW", "MEDIUM", "HIGH", "MAXIMUM"}),
      v = token(vertical, {"Auto", "1", "2", "3", "4", "5"}),
      h = token(horizontal, {"Auto", "Left", "Middle Left", "Middle", "Middle Right", "Right"}),
      sw = token(swing, {"OFF", "VERTICAL"}), pr = token(preset, {"NONE", "AWAY", "FROST_PROTECTION"}),
      md = token(mild, {"OFF", "ON"}), ix = token(iauto, {"OFF", "ON"});
  ESP_LOGI(TAG,
           "API request power=%d mode=%d temp=%d fan=%d vertical=%d horizontal=%d swing=%d preset=%d mild=%d iauto=%d "
           "(-1 omitted)",
           p, m, temperature, f, v, h, sw, pr, md, ix);
  if (std::min({p, m, f, v, h, sw, pr, md, ix}) < -1 ||
      (temperature != -1 && (temperature < TEMP_MIN || temperature > TEMP_MAX)) || (m == 5 && p == 1) ||
      (v >= 0 && sw >= 0 && (v == 0) != (sw == 1)))
    return false;
  const climate::ClimateMode modes[] = {climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_DRY,
                                        climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
                                        climate::CLIMATE_MODE_FAN_ONLY};
  Mode wire = this->last_mode_;
  auto resolved = this->mode;
  if (resolved == climate::CLIMATE_MODE_OFF) {
    switch (wire) {
      case Mode::DRY:
        resolved = modes[1];
        break;
      case Mode::COOL:
        resolved = modes[2];
        break;
      case Mode::HEAT:
        resolved = modes[3];
        break;
      case Mode::FAN:
        resolved = modes[4];
        break;
      default:
        resolved = modes[0];
    }
  }
  bool on = this->mode != climate::CLIMATE_MODE_OFF;
  float temp = this->target_temperature;
  if (ix == 0 && this->iauto_) {
    resolved = this->return_mode_;
    temp = this->return_temperature_;
  }
  if (m >= 0 && m != 5)
    resolved = modes[m == 6 ? 0 : m];
  if (m == 5)
    on = false;
  bool frost = pr < 0 ? this->preset.value_or(climate::CLIMATE_PRESET_NONE) == climate::CLIMATE_PRESET_AWAY : pr != 0;
  if (pr > 0) {
    if ((m >= 0 && m != 3) || p == 0)
      return false;
    resolved = modes[3];
    on = true;
  }
  if (p >= 0)
    on = p;
  bool active = ix >= 0 ? ix == 1 : this->iauto_ && resolved == modes[0] && on;
  if (ix == 1) {
    if (p == 0 || (m >= 0 && m != 0 && m != 6))
      return false;
    resolved = modes[0];
    on = true;
    temp = 25;
  }
  if (temperature != -1)
    temp = temperature;
  if ((active && (temp != 25 || frost)) || (md == 1 && (!on || resolved != modes[2])) ||
      (frost && on && (resolved != modes[3] || temperature != -1 || (f >= 0 && f != 5) || sw == 0 || v > 0)))
    return false;
  if (active && !this->iauto_) {
    this->return_mode_ = this->mode == climate::CLIMATE_MODE_OFF ? modes[0] : this->mode;
    this->return_temperature_ = this->target_temperature;
  }
  this->iauto_ = active;
  if (md >= 0)
    this->mild_dry_ = md == 1;
  this->mode = on ? resolved : climate::CLIMATE_MODE_OFF;
  this->last_mode_ = resolved == modes[1]   ? Mode::DRY
                     : resolved == modes[2] ? Mode::COOL
                     : resolved == modes[3] ? Mode::HEAT
                     : resolved == modes[4] ? Mode::FAN
                                            : Mode::AUTO;
  this->target_temperature = temp;
  this->preset = frost ? climate::CLIMATE_PRESET_AWAY : climate::CLIMATE_PRESET_NONE;
  if (f == 1 || f == 5)
    this->set_custom_fan_mode_(f == 1 ? FAN_MODE_MINIMUM : FAN_MODE_MAXIMUM);
  else if (f >= 0)
    this->set_fan_mode_(f == 2   ? climate::CLIMATE_FAN_LOW
                        : f == 3 ? climate::CLIMATE_FAN_MEDIUM
                        : f == 4 ? climate::CLIMATE_FAN_HIGH
                                 : climate::CLIMATE_FAN_AUTO);
  if (v > 0)
    this->vane_position_ = v;
  if (v >= 0 || sw >= 0)
    this->swing_mode = (v == 0 || sw == 1) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
  if (h >= 0)
    this->horizontal_vane_position_ = progmem_read_byte(&HORIZONTAL_VANE_POSITIONS[h]);
  uint8_t state[STATE_LENGTH];
  this->build_state_(state);
  char hex[format_hex_pretty_size(STATE_LENGTH)];
  ESP_LOGI(TAG,
           "API resolved power=%u mode=%u target=%.0f fan=%u vertical=%u horizontal=%u frost=%u mild=%u iauto=%u "
           "checksum=%02X frame=%s",
           on, (unsigned) this->last_mode_, this->target_temperature, this->fan_speed_(), this->vane_v_(),
           this->horizontal_vane_position_, frost, this->mild_dry_, active, state[26],
           format_hex_pretty_to(hex, state, STATE_LENGTH, ' '));
  this->send_state_(state);
  this->sync_vane_select_();
  this->sync_horizontal_vane_select_();
  this->publish_state();
  if (md == 1 && this->timer_controller)
    this->timer_controller->observe_features(false);
  return true;
}
void PanasonicIrClimate::send_short(uint8_t a, uint8_t b) {
  if (this->model_ != Model::RKR)
    return;
  uint8_t payload[]{0x80, a, b}, frame[SHORT_COMMAND_LENGTH];
  encode_short_command(payload, frame);
  this->echo_length_ = sizeof(frame);
  this->echo_sent_ms_ = millis();
  memcpy(this->echo_, frame + 8, 8);
  char hex[format_hex_pretty_size(SHORT_COMMAND_LENGTH)];
  ESP_LOGI(TAG, "TX short: %s", format_hex_pretty_to(hex, frame, sizeof(frame), ' '));
  auto tx = this->transmitter_->transmit();
  encode_waveform(*tx.get_data(), frame);
  tx.perform();
}
void PanasonicIrClimate::toggle_iauto() {
  if (this->model_ != Model::RKR)
    return;
  if (!this->iauto_) {
    this->return_mode_ = this->mode == climate::CLIMATE_MODE_OFF ? climate::CLIMATE_MODE_HEAT_COOL : this->mode;
    this->return_temperature_ = this->target_temperature;
    this->mode = climate::CLIMATE_MODE_HEAT_COOL;
    this->target_temperature = 25;
    this->iauto_ = true;
  } else {
    this->mode = this->return_mode_ == climate::CLIMATE_MODE_OFF ? climate::CLIMATE_MODE_HEAT_COOL : this->return_mode_;
    this->target_temperature = this->return_temperature_;
    this->iauto_ = false;
  }
  this->preset = climate::CLIMATE_PRESET_NONE;
  this->transmit_state();
  this->publish_state();
}
#endif

void PanasonicIrClimate::transmit_state() {
  uint8_t state[STATE_LENGTH];
  this->build_state_(state);
  this->send_state_(state);
  this->sync_vane_select_();
  this->sync_horizontal_vane_select_();
}

void PanasonicIrClimate::send_state_(const uint8_t (&state)[STATE_LENGTH]) {
  char hex[format_hex_pretty_size(STATE_LENGTH)];
  ESP_LOGD(TAG, "TX full state B13=%02X B19=%02X B20=%02X B25=%02X: %s", state[13], state[19], state[20], state[25],
           format_hex_pretty_to(hex, state, STATE_LENGTH, ' '));
  this->echo_length_ = STATE_LENGTH;
  memcpy(this->echo_, state + SECTION1_LENGTH, SECTION2_LENGTH);
  this->echo_sent_ms_ = millis();

  auto transmit = this->transmitter_->transmit();
  encode_waveform(*transmit.get_data(), state);

  transmit.perform();
}

bool PanasonicIrClimate::on_receive(remote_base::RemoteReceiveData data) {
  const Command command = read_command(data);
  const CommandStatus status = classify_command(command, this->model_);
  if (status == CommandStatus::COMMAND_STATUS_UNRELATED)
    return false;
  this->log_command_(command, status);
  if (status == CommandStatus::COMMAND_STATUS_INCOMPLETE || status == CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM)
    return false;
  const uint8_t *state = command.bytes;
  if (command.length == this->echo_length_ && millis() - this->echo_sent_ms_ < ECHO_WINDOW_MS &&
      memcmp(state + SECTION1_LENGTH, this->echo_, command.length - SECTION1_LENGTH) == 0) {
    this->echo_sent_ms_ -= ECHO_WINDOW_MS;
    return true;
  }
  if (command.length != STATE_LENGTH) {
#ifdef USE_PANASONIC_RKR_TIMERS
    if (!this->timer_controller || status != CommandStatus::COMMAND_STATUS_VALID_KNOWN)
      return false;
    auto action = short_action(command, this->model_);
    if (action == ShortAction::SHORT_ACTION_CANCEL_ALL)
      this->timer_controller->cancel_all(false);
    if (action == ShortAction::SHORT_ACTION_ECONAVI_ON || action == ShortAction::SHORT_ACTION_ECONAVI_OFF)
      this->timer_controller->observe_features(action == ShortAction::SHORT_ACTION_ECONAVI_ON);
    return true;
#else
    return false;
#endif
  }
  FrameParams params;
  params.model = this->model_;
  if (!decode_frame(state, params)) {
    ESP_LOGD(TAG, "Received frame is not a valid Panasonic state");
    return false;
  }

  if (this->model_ == Model::RKR) {
    if (state[23] == 0x89) {
      bool active = params.power && params.mode == Mode::AUTO && (state[25] & 0x80);
      if (active && !this->iauto_) {
        this->return_mode_ = this->mode;
        this->return_temperature_ = this->target_temperature;
      }
      this->iauto_ = active;
      if (params.mode == Mode::COOL && (state[15] == 0x7F || state[15] == 0x80)) {
        this->mild_dry_ = state[15] == 0x7F;
#ifdef USE_PANASONIC_RKR_TIMERS
        if (this->mild_dry_ && this->timer_controller)
          this->timer_controller->observe_features(false);
#endif
      }
    }
#ifdef USE_PANASONIC_RKR_TIMERS
    // Only this ordinary OFF pattern was isolated on hardware: ON1 specifically was cancelled.
    if (this->timer_controller && state[13] == 0x68 && has_rkr_ordinary_timer_fields(command) &&
        status == CommandStatus::COMMAND_STATUS_VALID_KNOWN) {
      this->timer_controller->observe_on1_cancel();
    }
    if (has_first_section(state, command.length) && state[23] == 0x80 && (state[13] & 8) && this->timer_controller)
      this->timer_controller->receive_timer(decode_rkr_timers(state));
#endif
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
  if (supports_horizontal_vane(this->model_) && valid_horizontal_vane(params.vane_h))
    this->horizontal_vane_position_ = params.vane_h;
  this->sync_horizontal_vane_select_();

  this->publish_state();
}

}  // namespace esphome::panasonic_ir
