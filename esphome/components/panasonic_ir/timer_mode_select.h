#pragma once
#include "panasonic_rkr_timers.h"
#if defined(USE_PANASONIC_RKR_TIMERS) && defined(USE_SELECT)
namespace esphome::panasonic_ir {
class RkrTimerModeSelect : public select::Select, public Component {
 public:
  RkrTimerModeSelect(RkrTimers *hub, TimerSlot slot) : hub_(hub), slot_(slot) {}
  void setup() override {
    this->hub_->mode_entities[static_cast<unsigned>(this->slot_)] = this;
    this->publish_state(size_t(this->hub_->get_mode(this->slot_)));
  }

 protected:
  void control(size_t index) override { this->hub_->set_mode(this->slot_, index); }
  RkrTimers *hub_;
  TimerSlot slot_;
};
}  // namespace esphome::panasonic_ir
#endif
