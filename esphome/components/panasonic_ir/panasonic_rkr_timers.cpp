#include "timer_datetime.h"
#include "panasonic_rkr_timers.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::panasonic_ir {
ESPHOME_LOG_TAG(TAG, "panasonic_ir.timers");
PROGMEM_STRING_TABLE(TimerSlotNames, "ON1", "ON2", "OFF1", "OFF2");

void RkrTimers::setup() {
  this->preference_ = this->climate_->make_entity_preference<RkrTimerSettings>(0x524B5202);
  RkrTimerSettings restored;
  bool valid = this->preference_.load(&restored);
  for (size_t i = 0; i < 4; i++)
    valid = valid && valid_timer_minutes(restored.minutes[i]) && restored.enabled[i] <= 1;
  if (valid) {
    this->settings_ = restored;
  }
  // Restoring intent never schedules or sends IR, including after time sync.
}

void RkrTimers::dump_config() {
  ESP_LOGCONFIG(TAG, "RKR timers (requested settings):");
  for (size_t i = 0; i < 4; i++) {
    ESP_LOGCONFIG(TAG, "  %s: %02u:%02u, enabled: %s",
                  LOG_STR_ARG(TimerSlotNames::get_log_str(i, TimerSlotNames::LAST_INDEX)),
                  this->settings_.minutes[i] / 60, this->settings_.minutes[i] % 60, ONOFF(this->settings_.enabled[i]));
  }
}

void RkrTimers::save_() {
  if (!this->preference_.save(&this->settings_)) {
    ESP_LOGW(TAG, "Could not save timer settings; restored settings may be outdated after restart");
  }
}

uint16_t RkrTimers::get_minutes(TimerSlot slot) const {
  return valid_timer_slot(slot) ? this->settings_.minutes[static_cast<uint8_t>(slot)] : 0;
}

bool RkrTimers::get_enabled(TimerSlot slot) const {
  return valid_timer_slot(slot) && this->settings_.enabled[static_cast<uint8_t>(slot)];
}

bool RkrTimers::set_minutes(TimerSlot slot, uint16_t minutes) {
  return this->set_timer(slot, minutes, this->get_enabled(slot));
}

void RkrTimers::set_enabled(TimerSlot slot, bool enabled) { this->set_timer(slot, this->get_minutes(slot), enabled); }

bool RkrTimers::apply(TimerSlot slot) {
  return this->set_timer(slot, this->get_minutes(slot), this->get_enabled(slot));
}

bool RkrTimers::set_timer(TimerSlot slot, uint16_t minutes, bool enabled, optional<bool> power,
                          const uint8_t (*operating_frame)[STATE_LENGTH]) {
  if (!valid_timer_slot(slot) || !valid_timer_minutes(minutes)) {
    ESP_LOGW(TAG, "Invalid timer request: slot %u, minutes %u", static_cast<unsigned>(slot), minutes);
    return false;
  }
  const auto now = this->clock_->now();
  RkrTimerFields fields;
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Timer request refused: clock has not synchronised");
    return false;
  }
  if (!make_rkr_timer_operation(slot, minutes, enabled, now.hour * 60 + now.minute, fields)) {
    ESP_LOGW(TAG, "Timer request refused: invalid clock time");
    return false;
  }
  if (!this->climate_->send_timer_state(fields, power, operating_frame))
    return false;
  const auto index = static_cast<size_t>(slot);
  this->settings_.minutes[index] = minutes;
  this->settings_.enabled[index] = enabled;
  this->save_();
  this->refresh_(index);
  return true;
}

bool PanasonicIrClimate::send_timer_state(const RkrTimerFields &timers, optional<bool> power,
                                          const uint8_t (*operating_frame)[STATE_LENGTH]) {
  const bool preserve_on = timers.on_enabled && timers.on_minutes == TIMER_TIME_SPECIAL;
  const bool preserve_off = timers.off_enabled && timers.off_minutes == TIMER_TIME_SPECIAL;
  if (this->model_ != Model::RKR || !timers.on_unknown_flag || !timers.off_unknown_flag ||
      !((preserve_on && valid_timer_minutes(timers.off_minutes)) ||
        (preserve_off && valid_timer_minutes(timers.on_minutes)))) {
    ESP_LOGW(TAG, "Timer request refused: unsupported model or invalid timer fields");
    return false;
  }
  uint8_t state[STATE_LENGTH];
  if (operating_frame != nullptr) {
    FrameParams params;
    params.model = Model::RKR;
    if ((*operating_frame)[23] != 0x89 || !decode_frame(*operating_frame, params)) {
      ESP_LOGW(TAG, "Timer request refused: invalid operating frame");
      return false;
    }
    memcpy(state, *operating_frame, sizeof(state));
  } else {
    this->build_state_(state);
  }
  if (power.has_value())
    state[13] = (state[13] & ~1U) | *power;
  if (!encode_rkr_timers(timers, state)) {
    ESP_LOGW(TAG, "Timer request refused: invalid timer time");
    return false;
  }
  this->send_state_(state, STATE_LENGTH);
  return true;
}

void PanasonicIrClimate::send_cancel_all() {
  uint8_t frame[]{0x02, 0x20, 0xE0, 0x04, 0, 0, 0, 6, 0x02, 0x20, 0xE0, 0x04, 0x80, 0xB6, 0x32, 0};
  frame[15] = calc_checksum(frame, sizeof(frame));
  this->send_state_(frame, sizeof(frame));
}

void RkrTimers::cancel_all() {
  this->climate_->send_cancel_all();
  this->observe_cancel_all();
}

void RkrTimers::observe_cancel_all() {
  for (size_t i = 0; i < 4; ++i) {
    this->settings_.enabled[i] = false;
    this->refresh_(i);
  }
  this->save_();
}

void RkrTimers::refresh_(size_t i) {
#ifdef USE_SWITCH
  if (this->switches_[i])
    this->switches_[i]->publish_state(this->settings_.enabled[i]);
#endif
#ifdef USE_DATETIME_TIME
  if (this->times_[i])
    this->times_[i]->publish_minutes(this->settings_.minutes[i]);
#endif
}

void RkrTimers::receive_timer(const RkrTimerFields &fields) {
  if (!valid_timer_field(fields.on_minutes) || !valid_timer_field(fields.off_minutes) || fields.clock_minutes >= 1440)
    return;
  const bool both_real = fields.on_minutes != TIMER_TIME_SPECIAL && fields.off_minutes != TIMER_TIME_SPECIAL;
  for (size_t type = 0; type < 2; ++type) {
    const auto minutes = type ? fields.off_minutes : fields.on_minutes;
    const bool enabled = type ? fields.off_enabled : fields.on_enabled;
    if ((both_real && !enabled) || (minutes == TIMER_TIME_SPECIAL && (enabled || type == 0)))
      continue;
    const size_t i = type ? 2 + fields.off_slot2 : fields.on_slot2;
    if (minutes != TIMER_TIME_SPECIAL)
      this->settings_.minutes[i] = minutes;
    this->settings_.enabled[i] = enabled;
    this->refresh_(i);
  }
  this->save_();
}

}  // namespace esphome::panasonic_ir
#endif
