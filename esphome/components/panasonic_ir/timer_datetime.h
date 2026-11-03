#pragma once
#include "panasonic_rkr_timers.h"
#if defined(USE_PANASONIC_RKR_TIMERS) && defined(USE_DATETIME_TIME)
#include "esphome/components/datetime/time_entity.h"
#include "esphome/core/log.h"

namespace esphome::panasonic_ir {
class RkrTimerTime : public datetime::TimeEntity, public Component {
 public:
  RkrTimerTime(RkrTimers *timers, TimerSlot slot) : timers_(timers), slot_(slot) {}
  void setup() override { this->publish_intent_(); }

 protected:
  void control(const datetime::TimeCall &call) override {
    const uint8_t hour = call.get_hour().value_or(this->hour_);
    const uint8_t minute = call.get_minute().value_or(this->minute_);
    const uint8_t second = call.get_second().value_or(this->second_);
    if (second == 0)
      this->timers_->set_minutes(this->slot_, hour * 60 + minute);
    else
      ESP_LOGW("panasonic_ir.timers", "Nonzero timer seconds rejected");
    this->publish_intent_();
  }
  void publish_intent_() {
    const auto minutes = this->timers_->get_minutes(this->slot_);
    this->hour_ = minutes / 60;
    this->minute_ = minutes % 60;
    this->second_ = 0;
    this->publish_state();
  }
  RkrTimers *timers_;
  TimerSlot slot_;
};
}  // namespace esphome::panasonic_ir
#endif
