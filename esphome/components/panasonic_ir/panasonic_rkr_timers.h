#pragma once

#include "esphome/core/defines.h"
#ifdef USE_PANASONIC_RKR_TIMERS
#include "panasonic_ir.h"
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/preferences.h"

namespace esphome::panasonic_ir {

struct RkrTimerSettings {
  uint16_t minutes[4]{360, 360, 1410, 1410};
  uint8_t enabled[4]{};  // 0 disabled, 1 daily, 2 once
  uint16_t submitted[4]{};
  uint32_t deadline[4]{};
};

// Editable intent and (optionally) recognised remote commands; not acknowledged indoor-unit state.
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
  bool api_set(StringRef timer, int slot, StringRef time, int minutes_from_now, StringRef timer_mode);
  void set_mode(TimerSlot slot, size_t mode);
  uint8_t get_mode(TimerSlot slot) const { return this->settings_.enabled[static_cast<unsigned>(slot)]; }
  void set_accept_received_timers(bool accept) { this->accept_ = accept; }
  void observe_on1_cancel();
  void receive_timer(const RkrTimerFields &fields);
  void cancel_all(bool transmit = true);
  void feature(uint8_t action);
  void observe_features(bool eco);
#ifdef USE_SELECT
  select::Select *mode_entities[4]{};
#endif
#ifdef USE_SWITCH
  switch_::Switch *eco_entity{};
#endif
  void (*refresh_times[4])(void *){};
  void *time_entities[4]{};
  void refresh_(unsigned i);
  void cleanup_();
  void committed_(unsigned i, const ESPTime &now);
  bool accept_{false};
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
