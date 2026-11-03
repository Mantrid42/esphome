#pragma once

#include "esphome/core/defines.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "panasonic_ir.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/preferences.h"

namespace esphome::panasonic_ir {

enum class TimerSlot : uint8_t { TIMER_SLOT_ON2, TIMER_SLOT_OFF2 };

struct RkrTimerSettings {
  uint16_t on_minutes{360};
  uint16_t off_minutes{1410};
  uint8_t on_enabled{0};
  uint8_t off_enabled{0};
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
  bool apply();

 protected:
  PanasonicIrClimate *climate_;
  time::RealTimeClock *clock_;
  ESPPreferenceObject preference_;
  RkrTimerSettings settings_{};
};

}  // namespace esphome::panasonic_ir
#endif
