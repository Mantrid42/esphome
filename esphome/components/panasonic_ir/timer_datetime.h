#pragma once
#include "panasonic_rkr_timers.h"
#if defined(USE_PANASONIC_RKR_TIMERS) && defined(USE_DATETIME_TIME)
#include "esphome/components/datetime/time_entity.h"
#include "esphome/core/log.h"

namespace esphome::panasonic_ir {
class RkrTimerTime : public datetime::TimeEntity, public Component {
 public:
  RkrTimerTime(RkrTimers *timers, TimerSlot slot) : timers_(timers), slot_(slot) {}
  void setup() override {
    this->timers_->register_time(this->slot_, this);
    this->publish_minutes(this->timers_->get_minutes(this->slot_));
  }

 protected:
  void control(const datetime::TimeCall &call) override {
    const uint8_t hour = call.get_hour().value_or(this->hour_);
    const uint8_t minute = call.get_minute().value_or(this->minute_);
    this->timers_->set_minutes(this->slot_, hour * 60 + minute / 10 * 10);
  }

 public:
  void publish_minutes(uint16_t minutes) {
    this->hour_ = minutes / 60;
    this->minute_ = minutes % 60;
    this->second_ = 0;
    this->publish_state();
  }

 protected:
  RkrTimers *timers_;
  TimerSlot slot_;
};
}  // namespace esphome::panasonic_ir
#endif
