#pragma once
#include "panasonic_rkr_timers.h"
#if defined(USE_PANASONIC_RKR_TIMERS) && defined(USE_SWITCH)
#include "esphome/components/switch/switch.h"
namespace esphome::panasonic_ir {
class EconaviSwitch : public switch_::Switch, public Component {
 public:
  explicit EconaviSwitch(RkrTimers *hub) : hub_(hub) {}
  void setup() override { this->hub_->eco_entity = this; }
  bool assumed_state() override { return true; }

 protected:
  void write_state(bool state) override { this->hub_->feature(state); }
  RkrTimers *hub_;
};
class RkrTimerSwitch : public switch_::Switch, public Component {
 public:
  RkrTimerSwitch(RkrTimers *timers, TimerSlot slot) : timers_(timers), slot_(slot) {}
  void setup() override { this->publish_state(this->timers_->get_enabled(this->slot_)); }
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
