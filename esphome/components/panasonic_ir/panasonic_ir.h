#pragma once

#include <iterator>

#include "esphome/components/climate_ir/climate_ir.h"
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#include "panasonic_ac_frame.h"

namespace esphome::panasonic_ir {

static constexpr const char *FAN_MODE_MINIMUM = "Minimum";
static constexpr const char *FAN_MODE_MAXIMUM = "Maximum";
static constexpr const char *const CUSTOM_FAN_MODES[] PROGMEM = {FAN_MODE_MINIMUM, FAN_MODE_MAXIMUM};

struct RkrTimerFields;
class RkrTimers;
class PanasonicIrClimate final : public climate_ir::ClimateIR {
 public:
  PanasonicIrClimate()
      : climate_ir::ClimateIR(TEMP_MIN, TEMP_MAX,
                              1.0f,  // Temp step
                              true,  // Supports dry
                              true,  // Supports fan only
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                               climate::CLIMATE_FAN_HIGH},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL},
                              {climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_AWAY}) {
    this->set_supported_custom_fan_modes_static(CUSTOM_FAN_MODES, std::size(CUSTOM_FAN_MODES));
  }

  void set_model(Model model) { this->model_ = model; }
#ifdef USE_PANASONIC_RKR_TIMERS
  void set_timer_controller(RkrTimers *controller) { this->timer_controller_ = controller; }
  bool send_timer_state(const RkrTimerFields &timers, optional<bool> power = {},
                        const uint8_t (*operating_frame)[STATE_LENGTH] = nullptr);
  void send_cancel_all();
#endif
  void setup() override;
  void dump_config() override;

  /// Set the vertical vane from the select: 0 is auto (swing vertical), 1 (highest) to 5 (lowest) are fixed positions.
  void set_vane_position(size_t index);
  /// Set the fixed vertical vane sent until the user selects another position, without transmitting at startup.
  void set_vertical_vane_default(uint8_t position) { this->vane_position_ = position; }
  /// Set the horizontal vane: 0 is auto, 1 far left, 2 left, 3 middle, 4 right, 5 far right.
  void set_horizontal_vane_position(size_t index);
#ifdef USE_SELECT
  SUB_SELECT(vane)
  SUB_SELECT(horizontal_vane)
#endif

  void enable_iauto();
  void send_powerful_quiet() { this->send_short_(0x9C, 0x32); }
  void send_nanoe_g() { this->send_short_(0x61, 0x33); }
#ifdef USE_SWITCH
  SUB_SWITCH(econavi)
  SUB_SWITCH(mild_dry)
  void set_econavi(bool enabled);
  void set_mild_dry(bool enabled);
#endif

 protected:
  void control(const climate::ClimateCall &call) override;

  /// Transmit via IR the state of this climate controller.
  void transmit_state() override;
  /// Handle received IR buffer
  bool on_receive(remote_base::RemoteReceiveData data) override;

  uint8_t fan_speed_() const;
  uint8_t vane_v_() const;
  void sync_vane_select_();
  void sync_horizontal_vane_select_();
  void apply_received_(const FrameParams &params);

  void build_state_(uint8_t *state);
  void send_state_(const uint8_t *state, size_t length);
#ifdef USE_PANASONIC_RKR_TIMERS
  RkrTimers *timer_controller_{nullptr};
#endif
  void send_short_(uint8_t first, uint8_t second);
  bool mild_dry_{false};
  bool iauto_{false};
  Model model_{Model::JKE};
  // The remote has no concept of "off" mode, so remember what to send along with the power bit
  Mode last_mode_{Mode::AUTO};
  // CKP models only have a power toggle, so we need to keep track of the state
  bool ckp_is_on_{false};
  // The time and second section of the last frame sent, so its echo is not taken for the remote. The time starts
  // outside the echo window.
  uint32_t echo_sent_ms_{UINT32_MAX / 2};
  uint8_t echo_[SECTION2_LENGTH]{};
  // The fixed vane position (1 to 5) used while swing is off. 0 until one is chosen, then heat blows down and the
  // other modes up
  uint8_t vane_position_{0};
  uint8_t horizontal_vane_position_{VANE_H_MIDDLE};
};

}  // namespace esphome::panasonic_ir
