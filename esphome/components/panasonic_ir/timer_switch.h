#pragma once
#include "panasonic_rkr_timers.h"
#if defined(USE_PANASONIC_RKR_TIMERS) && defined(USE_SWITCH)
#include "esphome/components/switch/switch.h"
namespace esphome::panasonic_ir {
class RkrTimerSwitch : public switch_::Switch, public Component {
 public:
  RkrTimerSwitch(RkrTimers *timers, TimerSlot slot) : timers_(timers), slot_(slot) {}
  void setup() override {
    this->timers_->register_switch(this->slot_, this);
    this->publish_state(this->timers_->get_enabled(this->slot_));
  }
  bool assumed_state() override { return true; }

 protected:
  void write_state(bool state) override {
    this->timers_->set_enabled(this->slot_, state);
    this->publish_state(this->timers_->get_enabled(this->slot_));
  }
  RkrTimers *timers_;
  TimerSlot slot_;
};
}  // namespace esphome::panasonic_ir
#endif
