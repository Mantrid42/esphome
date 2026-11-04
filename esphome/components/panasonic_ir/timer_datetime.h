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
    auto i = static_cast<unsigned>(this->slot_);
    this->timers_->time_entities[i] = this;
    this->timers_->refresh_times[i] = [](void *p) { static_cast<RkrTimerTime *>(p)->publish_intent_(); };
    this->publish_intent_();
  }

 protected:
  void control(const datetime::TimeCall &call) override;
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
