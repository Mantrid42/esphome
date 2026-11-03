#include "panasonic_rkr_timers.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "esphome/core/log.h"

namespace esphome::panasonic_ir {
ESPHOME_LOG_TAG(TAG, "panasonic_ir.timers");

void RkrTimers::setup() {
  this->preference_ = this->climate_->make_entity_preference<RkrTimerSettings>(0x524B5201);
  RkrTimerSettings restored;
  if (this->preference_.load(&restored) && valid_timer_minutes(restored.on_minutes) &&
      valid_timer_minutes(restored.off_minutes) && restored.on_enabled <= 1 && restored.off_enabled <= 1)
    this->settings_ = restored;
  // Restoring intent must never send IR, even once the clock becomes valid.
}

uint16_t RkrTimers::get_minutes(TimerSlot slot) const {
  return slot == TimerSlot::TIMER_SLOT_ON2 ? this->settings_.on_minutes : this->settings_.off_minutes;
}

bool RkrTimers::get_enabled(TimerSlot slot) const {
  return slot == TimerSlot::TIMER_SLOT_ON2 ? this->settings_.on_enabled : this->settings_.off_enabled;
}

bool RkrTimers::set_minutes(TimerSlot slot, uint16_t minutes) {
  if (!valid_timer_minutes(minutes)) {
    ESP_LOGW(TAG, "Timer time must be HH:00/10/20/30/40/50 with zero seconds; rejected");
    return false;
  }
  if (slot == TimerSlot::TIMER_SLOT_ON2)
    this->settings_.on_minutes = minutes;
  else
    this->settings_.off_minutes = minutes;
  this->preference_.save(&this->settings_);
  this->apply();
  return true;
}

void RkrTimers::set_enabled(TimerSlot slot, bool enabled) {
  if (slot == TimerSlot::TIMER_SLOT_ON2)
    this->settings_.on_enabled = enabled;
  else
    this->settings_.off_enabled = enabled;
  this->preference_.save(&this->settings_);
  this->apply();
}

bool RkrTimers::apply() {
  const auto now = this->clock_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Timer intent saved, but no IR sent: local clock invalid. Use Apply HA Timers after time sync");
    return false;
  }
  const RkrTimerFields timers{.on_minutes = this->settings_.on_minutes,
                              .off_minutes = this->settings_.off_minutes,
                              .clock_minutes = static_cast<uint16_t>(now.hour * 60 + now.minute),
                              .on_enabled = this->settings_.on_enabled != 0,
                              .off_enabled = this->settings_.off_enabled != 0};
  return this->climate_->send_timer_state(timers);
}

bool PanasonicIrClimate::send_timer_state(const RkrTimerFields &timers) {
  if (this->model_ != Model::RKR || !timers.on_slot2 || !timers.off_slot2 || !valid_timer_minutes(timers.on_minutes) ||
      !valid_timer_minutes(timers.off_minutes) || timers.clock_minutes >= 1440)
    return false;
  uint8_t state[STATE_LENGTH];
  this->build_state_(state);
  if (!encode_rkr_timers(timers, state))
    return false;
  char hex[format_hex_pretty_size(STATE_LENGTH)];
  ESP_LOGI(TAG, "TX experimental HA slots: ON2=%02u:%02u enabled=%u; OFF2=%02u:%02u enabled=%u; local clock=%02u:%02u",
           timers.on_minutes / 60, timers.on_minutes % 60, timers.on_enabled, timers.off_minutes / 60,
           timers.off_minutes % 60, timers.off_enabled, timers.clock_minutes / 60, timers.clock_minutes % 60);
  ESP_LOGI(TAG, "  B13=%02X B19=%02X B20=%02X B25=%02X; update flags=%u/%u; slot-1 preservation unverified", state[13],
           state[19], state[20], state[25], timers.update_on, timers.update_off);
  ESP_LOGI(TAG, "  Bytes: %s", format_hex_pretty_to(hex, state, STATE_LENGTH, ' '));
  this->send_state_(state);
  return true;
}

}  // namespace esphome::panasonic_ir
#endif
