#include "timer_datetime.h"
#include "panasonic_rkr_timers.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "esphome/core/log.h"

namespace esphome::panasonic_ir {
ESPHOME_LOG_TAG(TAG, "panasonic_ir.timers");

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

uint16_t RkrTimers::get_minutes(TimerSlot slot) const {
  return valid_timer_slot(slot) ? this->settings_.minutes[static_cast<uint8_t>(slot)] : 0;
}

bool RkrTimers::get_enabled(TimerSlot slot) const {
  return valid_timer_slot(slot) && this->settings_.enabled[static_cast<uint8_t>(slot)];
}

bool RkrTimers::set_minutes(TimerSlot slot, uint16_t minutes) {
  if (!valid_timer_slot(slot) || !valid_timer_minutes(minutes)) {
    ESP_LOGW(TAG, "Invalid timer slot/time: use HH:00/10/20/30/40/50 with zero seconds");
    return false;
  }
  this->settings_.minutes[static_cast<uint8_t>(slot)] = minutes;
  this->preference_.save(&this->settings_);
  this->apply(slot);
  return true;
}

void RkrTimers::set_enabled(TimerSlot slot, bool enabled) {
  if (!valid_timer_slot(slot))
    return;
  this->settings_.enabled[static_cast<uint8_t>(slot)] = enabled;
  this->preference_.save(&this->settings_);
  this->apply(slot);
}

bool RkrTimers::apply(TimerSlot slot) {
  if (!valid_timer_slot(slot))
    return false;
  const auto now = this->clock_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Timer intent saved, but no IR sent: local clock invalid. Apply again after time sync");
    return false;
  }
  RkrTimerFields fields;
  if (!make_rkr_timer_operation(slot, this->get_minutes(slot), this->get_enabled(slot), now.hour * 60 + now.minute,
                                fields))
    return false;
  return this->climate_->send_timer_state(fields);
}

bool PanasonicIrClimate::send_timer_state(const RkrTimerFields &timers) {
  const bool preserve_on = timers.on_enabled && timers.on_minutes == TIMER_TIME_SPECIAL;
  const bool preserve_off = timers.off_enabled && timers.off_minutes == TIMER_TIME_SPECIAL;
  if (this->model_ != Model::RKR || !timers.on_unknown_flag || !timers.off_unknown_flag ||
      !((preserve_on && valid_timer_minutes(timers.off_minutes)) ||
        (preserve_off && valid_timer_minutes(timers.on_minutes))))
    return false;
  uint8_t state[STATE_LENGTH];
  this->build_state_(state);
  if (!encode_rkr_timers(timers, state))
    return false;
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
  for (size_t i = 0; i < 4; ++i) {
    this->settings_.enabled[i] = false;
    this->refresh_(i);
  }
  this->preference_.save(&this->settings_);
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
  this->preference_.save(&this->settings_);
}

}  // namespace esphome::panasonic_ir
#endif
