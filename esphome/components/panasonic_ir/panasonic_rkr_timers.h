#pragma once

#include "esphome/core/defines.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "panasonic_ir.h"
#include "panasonic_rkr_timer_frame.h"
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/preferences.h"

namespace esphome::panasonic_ir {

class RkrTimerTime;
struct RkrTimerSettings {
  uint16_t minutes[4]{360, 360, 1410, 1410};
  uint8_t enabled[4]{};
};

// Stores requested settings and unambiguous updates received from a remote.
class RkrTimers : public Component {
 public:
  RkrTimers(PanasonicIrClimate *climate, time::RealTimeClock *clock) : climate_(climate), clock_(clock) {}
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA + 1; }
  uint16_t get_minutes(TimerSlot slot) const;
  bool get_enabled(TimerSlot slot) const;
  bool set_minutes(TimerSlot slot, uint16_t minutes);
  void set_enabled(TimerSlot slot, bool enabled);
  // Optional operating context applies to this frame only.
  bool set_timer(TimerSlot slot, uint16_t minutes, bool enabled, optional<bool> power = {},
                 const uint8_t (*operating_frame)[STATE_LENGTH] = nullptr);
  bool apply(TimerSlot slot);
  void cancel_all();
  void observe_cancel_all();
  void receive_timer(const RkrTimerFields &fields);
#ifdef USE_DATETIME_TIME
  void register_time(TimerSlot slot, RkrTimerTime *entity) { this->times_[static_cast<size_t>(slot)] = entity; }
#endif
#ifdef USE_SWITCH
  void register_switch(TimerSlot slot, switch_::Switch *entity) { this->switches_[static_cast<size_t>(slot)] = entity; }
#endif
 protected:
  void refresh_(size_t i);
#ifdef USE_DATETIME_TIME
  RkrTimerTime *times_[4]{};
#endif
#ifdef USE_SWITCH
  switch_::Switch *switches_[4]{};
#endif
  PanasonicIrClimate *climate_;
  time::RealTimeClock *clock_;
  ESPPreferenceObject preference_;
  RkrTimerSettings settings_{};
};

}  // namespace esphome::panasonic_ir
#endif
