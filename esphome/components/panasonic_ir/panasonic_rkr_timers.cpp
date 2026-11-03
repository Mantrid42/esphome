#include "panasonic_rkr_timers.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "esphome/core/log.h"

namespace esphome::panasonic_ir {
ESPHOME_LOG_TAG(TAG, "panasonic_ir.timers");

void RkrTimers::setup() {
  // Reserve the old preference layout as well, so ESP8266 preference offsets remain stable.
  struct LegacySettings {
    uint16_t on_minutes;
    uint16_t off_minutes;
    uint8_t on_enabled;
    uint8_t off_enabled;
  };
  auto legacy_preference = this->climate_->make_entity_preference<LegacySettings>(0x524B5201);
  this->preference_ = this->climate_->make_entity_preference<RkrTimerSettings>(0x524B5202);
  RkrTimerSettings restored;
  bool valid = this->preference_.load(&restored);
  for (size_t i = 0; i < 4; i++)
    valid = valid && valid_timer_minutes(restored.minutes[i]) && restored.enabled[i] <= 1;
  if (valid) {
    this->settings_ = restored;
  } else {
    LegacySettings legacy{};
    if (legacy_preference.load(&legacy) && valid_timer_minutes(legacy.on_minutes) &&
        valid_timer_minutes(legacy.off_minutes) && legacy.on_enabled <= 1 && legacy.off_enabled <= 1) {
      this->settings_.minutes[1] = legacy.on_minutes;
      this->settings_.minutes[3] = legacy.off_minutes;
      this->settings_.enabled[1] = legacy.on_enabled;
      this->settings_.enabled[3] = legacy.off_enabled;
      this->preference_.save(&this->settings_);
    }
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
  this->pending_slots_ &= ~(1U << static_cast<uint8_t>(slot));
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

bool RkrTimers::apply_all() {
  if (!this->clock_->now().is_valid()) {
    this->pending_slots_ = 0;
    this->cancel_interval(APPLY_INTERVAL_ID);
    ESP_LOGW(TAG, "Apply All skipped: local clock invalid; no automatic retry");
    return false;
  }
  this->pending_slots_ = 0x0F;
  this->set_interval(APPLY_INTERVAL_ID, 1000, [this]() { this->process_pending_(); });
  return true;
}

void RkrTimers::process_pending_() {
  for (uint8_t i = 0; i < 4; i++) {
    if ((this->pending_slots_ & (1U << i)) != 0) {
      if (!this->apply(static_cast<TimerSlot>(i)))
        this->pending_slots_ = 0;
      break;
    }
  }
  if (this->pending_slots_ == 0)
    this->cancel_interval(APPLY_INTERVAL_ID);
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
  char hex[format_hex_pretty_size(STATE_LENGTH)];
  ESP_LOGI(TAG, "TX Panasonic RKR timer operation; policy=UPDATE; local clock=%02u:%02u", timers.clock_minutes / 60,
           timers.clock_minutes % 60);
  if (timers.on_minutes == TIMER_TIME_SPECIAL) {
    ESP_LOGI(TAG, "  ON%u: preserve existing (enable=%u, special=0x600)", timers.on_slot2 ? 2 : 1, timers.on_enabled);
  } else {
    ESP_LOGI(TAG, "  ON%u: enabled=%u, %02u:%02u", timers.on_slot2 ? 2 : 1, timers.on_enabled, timers.on_minutes / 60,
             timers.on_minutes % 60);
  }
  if (timers.off_minutes == TIMER_TIME_SPECIAL) {
    ESP_LOGI(TAG, "  OFF%u: preserve existing (enable=%u, special=0x600)", timers.off_slot2 ? 2 : 1,
             timers.off_enabled);
  } else {
    ESP_LOGI(TAG, "  OFF%u: enabled=%u, %02u:%02u", timers.off_slot2 ? 2 : 1, timers.off_enabled,
             timers.off_minutes / 60, timers.off_minutes % 60);
  }
  ESP_LOGI(TAG, "  B13=%02X B19=%02X B20=%02X B23=%02X B25=%02X; unknown timer-adjacent flags=%u/%u", state[13],
           state[19], state[20], state[23], state[25], timers.on_unknown_flag, timers.off_unknown_flag);
  ESP_LOGI(TAG, "  Bytes: %s", format_hex_pretty_to(hex, state, STATE_LENGTH, ' '));
  this->send_state_(state);
  return true;
}

}  // namespace esphome::panasonic_ir
#endif
