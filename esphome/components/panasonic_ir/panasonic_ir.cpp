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
#ifdef USE_SWITCH
  if (this->econavi_switch_ != nullptr)
    this->horizontal_vane_position_ = VANE_H_AUTO;
#endif
  this->sync_vane_select_();
  this->sync_horizontal_vane_select_();
  this->sync_feature_switches_();
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
  if (this->iauto_ && !call.get_mode().has_value() &&
      (call.has_custom_fan_mode() ||
       call.get_fan_mode().value_or(climate::CLIMATE_FAN_AUTO) != climate::CLIMATE_FAN_AUTO))
    return;
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
    this->iauto_ = this->iauto_ && this->mode == climate::CLIMATE_MODE_HEAT_COOL;
    if (this->iauto_) {
      this->iauto_temperature_ = params.temperature;
      state[25] |= 0x80;
    }
    if (params.mode == Mode::COOL && this->mild_dry_)
      state[15] = 0x7F;
    state[13] &= ~0x08;  // Preserve hardware timers on operating commands.
    state[26] = calc_checksum(state, STATE_LENGTH);
  }
}

void PanasonicIrClimate::transmit_state() {
  uint8_t state[STATE_LENGTH];
  this->build_state_(state);
  this->send_state_(state, STATE_LENGTH);
  // A deferred explicit edit follows an ordinary local command, never a timer operation.
  if (this->econavi_pending_ && this->mode != climate::CLIMATE_MODE_OFF && !this->iauto_ &&
      this->mode != climate::CLIMATE_MODE_FAN_ONLY)
    this->send_econavi_();
  this->sync_feature_switches_();
  this->sync_vane_select_();
  this->sync_horizontal_vane_select_();
}

void PanasonicIrClimate::send_state_(const uint8_t *state, size_t length) {
  if (length == STATE_LENGTH) {
    if (this->model_ == Model::RKR && (state[17] != VANE_H_AUTO || state[15] == 0x7F)) {
      this->cancel_econavi_();
      this->sync_feature_switches_();
    }
    memcpy(this->echo_, state + SECTION1_LENGTH, SECTION2_LENGTH);
    this->echo_sent_ms_ = millis();
  } else {
    this->short_echo_action_ = encode_uint16(state[13], state[14]);
    this->short_echo_sent_ms_ = millis();
  }
  log_tx(state, length);

  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  data->set_carrier_frequency(CARRIER_FREQUENCY);
  // Two headers (mark + space) and footer marks, 16 timings per byte, and the gap
  data->reserve(2 * 3 + length * 16 + 1);

  data->item(HEADER_MARK, HEADER_SPACE);
  data->item_bytes<BIT_TIMINGS>(state, SECTION1_LENGTH);
  data->item(BIT_MARK, SECTION_GAP);
  data->item(HEADER_MARK, HEADER_SPACE);
  data->item_bytes<BIT_TIMINGS>(state + SECTION1_LENGTH, length - SECTION1_LENGTH);
  data->mark(BIT_MARK);

  transmit.perform();
}

void PanasonicIrClimate::send_short_(uint8_t first, uint8_t second) {
  if (this->model_ != Model::RKR)
    return;
  uint8_t frame[SHORT_COMMAND_LENGTH];
  encode_short_command({0x80, first, second}, frame);
  this->send_state_(frame, sizeof(frame));
}

void PanasonicIrClimate::enable_iauto() {
  if (this->model_ != Model::RKR)
    return;
  this->iauto_ = true;
  this->mode = climate::CLIMATE_MODE_HEAT_COOL;
  this->preset = climate::CLIMATE_PRESET_NONE;
  this->target_temperature = this->iauto_temperature_;
  this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
  this->transmit_state();
  this->publish_state();
}

bool PanasonicIrClimate::can_set_mild_dry() const {
  return this->model_ == Model::RKR &&
         (this->mode == climate::CLIMATE_MODE_OFF || this->mode == climate::CLIMATE_MODE_COOL);
}

bool PanasonicIrClimate::can_set_econavi() const {
  return this->model_ == Model::RKR && !this->mild_dry_ &&
         (this->mode == climate::CLIMATE_MODE_OFF || (!this->iauto_ && this->mode != climate::CLIMATE_MODE_FAN_ONLY));
}

bool PanasonicIrClimate::is_econavi_active() const {
  return this->econavi_latched_ && this->mode != climate::CLIMATE_MODE_OFF &&
         this->mode != climate::CLIMATE_MODE_FAN_ONLY && !this->iauto_ && !this->is_mild_dry_active();
}

void PanasonicIrClimate::sync_feature_switches_() {
#ifdef USE_SWITCH
  PUBLISH_STATE(this->econavi_switch_, this->econavi_);
  PUBLISH_STATE(this->mild_dry_switch_, this->mild_dry_);
#endif
}

void PanasonicIrClimate::cancel_econavi_() {
  this->econavi_ = false;
  this->econavi_latched_ = false;
  this->econavi_pending_ = false;
}

void PanasonicIrClimate::send_econavi_() {
  this->econavi_pending_ = false;
  this->econavi_latched_ = this->econavi_;
  this->send_short_(this->econavi_ ? 0x68 : 0x52, this->econavi_ ? 0xF2 : 0x33);
}

#ifdef USE_SWITCH
void PanasonicIrClimate::set_mild_dry(bool enabled) {
  if (this->model_ != Model::RKR || (enabled && !this->can_set_mild_dry())) {
    this->sync_feature_switches_();
    return;
  }
  const bool changed = this->mild_dry_ != enabled;
  this->mild_dry_ = enabled;
  if (enabled) {
    this->econavi_pending_ = this->econavi_pending_ || this->econavi_ || this->econavi_latched_;
    this->econavi_ = false;
  }
  if (changed && this->mode == climate::CLIMATE_MODE_COOL)
    this->transmit_state();
  this->sync_feature_switches_();
}

void PanasonicIrClimate::set_econavi(bool enabled) {
  if (this->model_ != Model::RKR || (enabled && !this->can_set_econavi())) {
    this->sync_feature_switches_();
    return;
  }
  const bool changed = this->econavi_ != enabled;
  this->econavi_ = enabled;
  if (enabled)
    this->horizontal_vane_position_ = VANE_H_AUTO;
  this->sync_horizontal_vane_select_();
  if (this->mode == climate::CLIMATE_MODE_OFF || this->iauto_ || this->mode == climate::CLIMATE_MODE_FAN_ONLY) {
    this->econavi_pending_ = this->econavi_pending_ || changed;
  } else {
    this->send_econavi_();
  }
  this->sync_feature_switches_();
}
#endif

bool PanasonicIrClimate::is_short_echo_(uint16_t action) {
  if (action != this->short_echo_action_ || millis() - this->short_echo_sent_ms_ >= ECHO_WINDOW_MS)
    return false;
  this->short_echo_sent_ms_ -= ECHO_WINDOW_MS;
  return true;
}

bool PanasonicIrClimate::on_receive(remote_base::RemoteReceiveData data) {
  uint8_t state[STATE_LENGTH];
  bool short_command = false;
  if (!read_state(data, state, this->model_ == Model::RKR ? &short_command : nullptr))
    return false;
  if (short_command) {
    const uint16_t action = encode_uint16(state[13], state[14]);
    // Preserve the existing observation of short echoes, but identify them in the log.
    const bool echo = this->is_short_echo_(action);
#ifdef USE_PANASONIC_RKR_TIMERS
    if (this->timer_controller_ && action == 0xB632) {
      if (echo) {
        ESP_LOGD(TAG, "ECHO: timer CANCEL ALL");
      } else {
        ESP_LOGD(TAG, "RX timer: CANCEL ALL (all slots disabled)");
      }
      this->timer_controller_->observe_cancel_all();
    } else
#endif
    {
      log_short_command(action, echo);
    }
    log_frame(echo ? LOG_STR("ECHO") : LOG_STR("RX"), state, SHORT_COMMAND_LENGTH);
    if (action == 0x5233) {
      this->cancel_econavi_();
    } else if (action == 0x68F2 && !this->mild_dry_) {
      this->econavi_ = true;
      this->econavi_latched_ = true;
      this->econavi_pending_ = false;
      this->horizontal_vane_position_ = VANE_H_AUTO;
    }
    this->sync_horizontal_vane_select_();
    this->sync_feature_switches_();
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
    ESP_LOGD(TAG, "ECHO: ignoring our own full frame");
    log_frame(LOG_STR("ECHO"), state, STATE_LENGTH);
    this->echo_sent_ms_ -= ECHO_WINDOW_MS;  // only the first echo
    return true;
  }

#ifdef USE_PANASONIC_RKR_TIMERS
  const bool timer_command = this->timer_controller_ && state[23] == 0x80 && (state[13] & 8);
  if (timer_command)
    this->timer_controller_->receive_timer(decode_rkr_timers(state));
#else
  constexpr bool timer_command = false;
#endif
  optional<bool> mild_dry;
  const bool rkr_operating = this->model_ == Model::RKR && state[23] == 0x89;
  if (rkr_operating) {
    this->iauto_ = params.power && params.mode == Mode::AUTO && (state[25] & 0x80);
    if (this->iauto_)
      this->iauto_temperature_ = params.temperature;
    if (params.mode == Mode::COOL && (state[15] == 0x7F || state[15] == 0x80)) {
      mild_dry = state[15] == 0x7F;
      this->mild_dry_ = *mild_dry;
      if (this->mild_dry_)
        this->cancel_econavi_();
    }
  }
  if (!timer_command)
    log_command(params, mild_dry, rkr_operating && this->iauto_);
  log_frame(LOG_STR("RX"), state, STATE_LENGTH);
  this->apply_received_(params);
  return true;
}

void PanasonicIrClimate::apply_received_(const FrameParams &params) {
  if (this->model_ == Model::RKR && valid_horizontal_vane(params.vane_h)) {
    this->horizontal_vane_position_ = params.vane_h;
    if (params.vane_h != VANE_H_AUTO)
      this->cancel_econavi_();
  }
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

  switch (this->iauto_ && params.mode == Mode::AUTO ? FAN_SPEED_AUTO : params.fan_speed) {
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

  this->sync_feature_switches_();
  this->publish_state();
}

}  // namespace esphome::panasonic_ir
