#include "panasonic_rkr_timers.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "esphome/core/log.h"
#include "timer_datetime.h"

namespace esphome::panasonic_ir {
ESPHOME_LOG_TAG(TAG, "panasonic_ir.timers");

#ifdef USE_DATETIME_TIME
void RkrTimerTime::control(const datetime::TimeCall &call) {
  const uint8_t hour = call.get_hour().value_or(this->hour_);
  const uint8_t minute = call.get_minute().value_or(this->minute_);
  const uint8_t second = call.get_second().value_or(this->second_);
  if (second == 0) {
    this->timers_->set_minutes(this->slot_, hour * 60 + minute);
  } else {
    ESP_LOGW(TAG, "Nonzero timer seconds rejected");
  }
  this->publish_intent_();
}
#endif

void RkrTimers::setup() {
  // Reserve the old preference layout as well, so ESP8266 preference offsets remain stable.
  struct LegacySettings {
    uint16_t on_minutes;
    uint16_t off_minutes;
    uint8_t on_enabled;
    uint8_t off_enabled;
  };
  auto legacy_preference = this->climate_->make_entity_preference<LegacySettings>(0x524B5201);
  struct Previous {
    uint16_t minutes[4];
    uint8_t enabled[4];
  };
  auto previous = this->climate_->make_entity_preference<Previous>(0x524B5202);
  this->preference_ = this->climate_->make_entity_preference<RkrTimerSettings>(0x524B5203, true);
  this->climate_->timer_controller = this;
  this->set_interval(10000, [this]() { this->cleanup_(); });
  RkrTimerSettings restored;
  bool valid = this->preference_.load(&restored);
  for (size_t i = 0; i < 4; i++)
    valid = valid && valid_timer_minutes(restored.minutes[i]) && restored.enabled[i] <= 2 &&
            (!restored.deadline[i] || (restored.enabled[i] == 2 && valid_timer_minutes(restored.submitted[i])));
  if (valid) {
    this->settings_ = restored;
  } else {
    Previous old{};
    if (previous.load(&old)) {
      for (unsigned i = 0; i < 4; i++) {
        if (valid_timer_minutes(old.minutes[i]) && old.enabled[i] <= 1) {
          this->settings_.minutes[i] = old.minutes[i];
          this->settings_.enabled[i] = old.enabled[i];
        }
      }
      this->preference_.save(&this->settings_);
      return;
    }
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
  return true;
}

void RkrTimers::set_enabled(TimerSlot slot, bool enabled) { this->set_mode(slot, enabled ? 1 : 0); }

void RkrTimers::refresh_(unsigned i) {
#ifdef USE_SELECT
  if (this->mode_entities[i])
    this->mode_entities[i]->publish_state(size_t(this->settings_.enabled[i]));
#endif
  if (this->refresh_times[i])
    this->refresh_times[i](this->time_entities[i]);
}

void RkrTimers::set_mode(TimerSlot slot, size_t mode) {
  if (!valid_timer_slot(slot) || mode > 2)
    return;
  auto i = static_cast<unsigned>(slot);
  this->settings_.enabled[i] = mode;
  if (mode != 2)
    this->settings_.deadline[i] = 0;
  this->preference_.save(&this->settings_);
  this->apply(slot);
  this->refresh_(i);
}

void RkrTimers::committed_(unsigned i, const ESPTime &now) {
  this->settings_.submitted[i] = this->settings_.minutes[i];
  this->settings_.deadline[i] = 0;
  if (this->settings_.enabled[i] == 2) {
    // Search real minute boundaries: a nonexistent wall time is skipped, a repeated one occurs twice.
    auto epoch = now.timestamp - now.second;
    for (unsigned n = 0; n < 3000; n++) {
      epoch += 60;
      auto local = ESPTime::from_epoch_local(epoch);
      if (local.hour * 60 + local.minute == this->settings_.submitted[i]) {
        this->settings_.deadline[i] = epoch + 1800;
        break;
      }
    }
  }
  ESP_LOGI(TAG, "Submitted %s%u: %s %02u:%02u, cleanup epoch=%u",
           i < 2 ? LOG_STR_LITERAL("ON") : LOG_STR_LITERAL("OFF"), (i % 2) + 1,
           this->settings_.enabled[i] == 2   ? LOG_STR_LITERAL("Enabled Once")
           : this->settings_.enabled[i] == 1 ? LOG_STR_LITERAL("Enabled Daily")
                                             : LOG_STR_LITERAL("Disabled"),
           this->settings_.submitted[i] / 60, this->settings_.submitted[i] % 60, this->settings_.deadline[i]);
  this->preference_.save(&this->settings_);
}

void RkrTimers::cleanup_() {
  auto now = this->clock_->now();
  if (!now.is_valid())
    return;
  for (unsigned i = 0; i < 4; i++) {
    if (!this->settings_.deadline[i] || now.timestamp < this->settings_.deadline[i])
      continue;
    RkrTimerFields fields;
    if (make_rkr_timer_operation(static_cast<TimerSlot>(i), this->settings_.submitted[i], false,
                                 now.hour * 60 + now.minute, fields) &&
        this->climate_->send_timer_state(fields)) {
      ESP_LOGI(TAG, "One-shot cleanup slot=%u submitted minutes=%u deadline=%u", i, this->settings_.submitted[i],
               this->settings_.deadline[i]);
      this->settings_.enabled[i] = 0;
      this->settings_.deadline[i] = 0;
      this->preference_.save(&this->settings_);
      this->refresh_(i);
    }
  }
}

bool RkrTimers::apply(TimerSlot slot) {
  if (!valid_timer_slot(slot))
    return false;
  // A newer individual request supersedes a queued batch.
  this->pending_slots_ = 0;
  const auto now = this->clock_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Timer intent saved, but no IR sent: local clock invalid. Apply again after time sync");
    return false;
  }
  RkrTimerFields fields;
  if (!make_rkr_timer_operation(slot, this->get_minutes(slot), this->get_enabled(slot), now.hour * 60 + now.minute,
                                fields))
    return false;
  if (!this->climate_->send_timer_state(fields))
    return false;
  this->committed_(static_cast<unsigned>(slot), now);
  return true;
}

bool RkrTimers::api_set(StringRef timer, int slot, StringRef time, int delay, StringRef mode) {
  ESP_LOGI(TAG, "API timer=%.*s slot=%d time=%.*s minutes_from_now=%d mode=%.*s", int(timer.size()), timer.c_str(),
           slot, int(time.size()), time.empty() ? "" : time.c_str(), delay, int(mode.size()),
           mode.empty() ? "" : mode.c_str());
  if ((timer != "ON" && timer != "OFF") || (slot != 1 && slot != 2) || delay < -1 || delay > 1440 ||
      (!time.empty() && delay != -1))
    return false;
  auto now = this->clock_->now();
  if (!now.is_valid())
    return false;
  unsigned i = (timer == "ON" ? 0 : 2) + slot - 1;
  unsigned minutes = this->settings_.minutes[i];
  unsigned requested = this->settings_.enabled[i];
  if (!mode.empty()) {
    if (mode == "disabled")
      requested = 0;
    else if (mode == "daily")
      requested = 1;
    else if (mode == "once")
      requested = 2;
    else
      return false;
  }
  if (!time.empty()) {
    if (time.size() != 5 || time[2] != ':')
      return false;
    for (unsigned j : {0U, 1U, 3U, 4U})
      if (time[j] < '0' || time[j] > '9')
        return false;
    unsigned mm = (time[3] - '0') * 10 + time[4] - '0';
    minutes = ((time[0] - '0') * 10 + time[1] - '0') * 60 + mm;
    if (mm >= 60 || !valid_timer_minutes(minutes))
      return false;
  } else if (delay != -1) {
    // Walk elapsed minutes, converting at each boundary so gaps and folds follow the local timezone.
    auto start = now.timestamp - now.second;
    unsigned first = delay + (now.second != 0);
    ESPTime target{};
    for (unsigned n = std::max(1U, first); n <= first + 10; n++) {
      target = ESPTime::from_epoch_local(start + n * 60);
      if (target.minute % 10 == 0)
        break;
    }
    minutes = target.hour * 60 + target.minute;
    // A daily hardware timer cannot skip an earlier occurrence of the same wall time.
    for (auto epoch = start + 60; epoch < target.timestamp; epoch += 60) {
      auto local = ESPTime::from_epoch_local(epoch);
      if (local.hour * 60 + local.minute == minutes)
        return false;
    }
    ESP_LOGI(TAG, "Requested delay %d min resolves to local %02u:%02u", delay, target.hour, target.minute);
  }
  RkrTimerFields fields;
  if (!make_rkr_timer_operation(static_cast<TimerSlot>(i), minutes, requested != 0, now.hour * 60 + now.minute,
                                fields) ||
      !this->climate_->send_timer_state(fields))
    return false;
  this->pending_slots_ = 0;
  this->settings_.minutes[i] = minutes;
  this->settings_.enabled[i] = requested;
  this->committed_(i, now);
  this->refresh_(i);
  return true;
}

bool RkrTimers::apply_all() {
  if (!this->clock_->now().is_valid()) {
    this->pending_slots_ = 0;
    this->cancel_interval(APPLY_INTERVAL_ID);
    ESP_LOGW(TAG, "Apply All skipped: local clock invalid; no automatic retry");
    return false;
  }
  this->pending_slots_ = 3;
  this->set_interval(APPLY_INTERVAL_ID, 1000, [this]() { this->process_pending_(); });
  return true;
}

void RkrTimers::process_pending_() {
  if (this->pending_slots_) {
    unsigned i = (this->pending_slots_ & 1) ? 0 : 1;
    auto now = this->clock_->now();
    RkrTimerFields f{.on_minutes = this->settings_.minutes[i],
                     .off_minutes = this->settings_.minutes[i + 2],
                     .clock_minutes = static_cast<uint16_t>(now.hour * 60 + now.minute),
                     .on_enabled = this->settings_.enabled[i] != 0,
                     .off_enabled = this->settings_.enabled[i + 2] != 0,
                     .on_slot2 = i == 1,
                     .off_slot2 = i == 1};
    if (now.is_valid() && this->climate_->send_timer_state(f)) {
      this->committed_(i, now);
      this->committed_(i + 2, now);
      this->pending_slots_ &= ~(1U << i);
    } else
      this->pending_slots_ = 0;
  }
  if (this->pending_slots_ == 0)
    this->cancel_interval(APPLY_INTERVAL_ID);
}

void RkrTimers::observe_on1_cancel() {
  this->pending_slots_ = 0;
  this->settings_.deadline[0] = 0;
  if (this->accept_)
    this->settings_.enabled[0] = 0;
  this->preference_.save(&this->settings_);
  this->refresh_(0);
}

void RkrTimers::receive_timer(const RkrTimerFields &f) {
  if (!valid_timer_field(f.on_minutes) || !valid_timer_field(f.off_minutes) || f.clock_minutes >= 1440)
    return;
  this->pending_slots_ = 0;
  if (!f.on_enabled && !f.off_enabled && valid_timer_minutes(f.on_minutes) && valid_timer_minutes(f.off_minutes)) {
    ESP_LOGW(TAG, "Ambiguous remote cancellation with two real times; not importing slot modes");
    this->settings_.deadline[f.on_slot2] = 0;
    this->settings_.deadline[2 + f.off_slot2] = 0;
    this->preference_.save(&this->settings_);
    return;
  }
  for (unsigned type = 0; type < 2; type++) {
    unsigned i = type ? 2 + f.off_slot2 : f.on_slot2;
    auto minutes = type ? f.off_minutes : f.on_minutes;
    bool enabled = type ? f.off_enabled : f.on_enabled;
    // A disabled OFF field with the sentinel cancelled the selected OFF slot in hardware.
    // It carries no replacement time. The equivalent ON variant is not established.
    if (minutes == TIMER_TIME_SPECIAL && (!type || enabled))
      continue;
    // Remote SET includes an inactive opposite time, not an instruction to import that slot.
    if (!enabled && (f.on_enabled || f.off_enabled) &&
        (f.on_minutes != TIMER_TIME_SPECIAL && f.off_minutes != TIMER_TIME_SPECIAL))
      continue;
    this->settings_.deadline[i] = 0;
    if (this->accept_) {
      if (minutes != TIMER_TIME_SPECIAL)
        this->settings_.minutes[i] = minutes;
      this->settings_.enabled[i] = enabled;
      this->refresh_(i);
    }
  }
  this->preference_.save(&this->settings_);
}

void RkrTimers::cancel_all(bool transmit) {
  if (transmit)
    this->climate_->send_short(0xB6, 0x32);
  this->pending_slots_ = 0;
  for (unsigned i = 0; i < 4; i++) {
    this->settings_.deadline[i] = 0;
    if (transmit || this->accept_)
      this->settings_.enabled[i] = 0;
    this->refresh_(i);
  }
  this->preference_.save(&this->settings_);
}

void RkrTimers::observe_features(bool eco) {
#ifdef USE_SWITCH
  if (this->eco_entity)
    this->eco_entity->publish_state(eco);
#endif
}
void RkrTimers::feature(uint8_t action) {
  if (action == 0 || action == 1) {
    this->climate_->send_short(action ? 0x68 : 0x52, action ? 0xF2 : 0x33);
    this->observe_features(action);
  }
  if (action == 2)
    this->climate_->send_short(0x9C, 0x32);
  if (action == 3)
    this->climate_->send_short(0x61, 0x33);
  if (action == 4)
    this->climate_->toggle_iauto();
}

bool PanasonicIrClimate::send_timer_state(const RkrTimerFields &timers) {
  const bool preserve_on = timers.on_enabled && timers.on_minutes == TIMER_TIME_SPECIAL;
  const bool preserve_off = timers.off_enabled && timers.off_minutes == TIMER_TIME_SPECIAL;
  if (this->model_ != Model::RKR || !timers.on_unknown_flag || !timers.off_unknown_flag ||
      !((preserve_on && valid_timer_minutes(timers.off_minutes)) ||
        (preserve_off && valid_timer_minutes(timers.on_minutes)) ||
        (valid_timer_minutes(timers.on_minutes) && valid_timer_minutes(timers.off_minutes) &&
         timers.on_slot2 == timers.off_slot2)))
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
