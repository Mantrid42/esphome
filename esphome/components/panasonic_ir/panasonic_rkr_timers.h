#pragma once

#include "esphome/core/defines.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "panasonic_ir.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/preferences.h"

namespace esphome::panasonic_ir {

struct RkrTimerSettings {
  uint16_t minutes[4]{360, 360, 1410, 1410};
  uint8_t enabled[4]{};
};

// This is desired HA state, never inferred from remote captures or an IR acknowledgement.
class RkrTimers : public Component {
 public:
  RkrTimers(PanasonicIrClimate *climate, time::RealTimeClock *clock) : climate_(climate), clock_(clock) {}
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA + 1; }
  uint16_t get_minutes(TimerSlot slot) const;
  bool get_enabled(TimerSlot slot) const;
  bool set_minutes(TimerSlot slot, uint16_t minutes);
  void set_enabled(TimerSlot slot, bool enabled);
  bool apply(TimerSlot slot);
  bool apply_all();
  bool apply() { return this->apply_all(); }  // Existing experimental Apply button.

 protected:
  static constexpr uint32_t APPLY_INTERVAL_ID = 0;
  void process_pending_();
  uint8_t pending_slots_{0};
  PanasonicIrClimate *climate_;
  time::RealTimeClock *clock_;
  ESPPreferenceObject preference_;
  RkrTimerSettings settings_{};
};

}  // namespace esphome::panasonic_ir
#endif
