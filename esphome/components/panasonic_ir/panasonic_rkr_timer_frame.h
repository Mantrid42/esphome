#pragma once

#include "panasonic_ac_frame.h"

namespace esphome::panasonic_ir {

static constexpr uint16_t TIMER_TIME_SPECIAL = 0x600;

enum class TimerSlot : uint8_t { TIMER_SLOT_ON1, TIMER_SLOT_ON2, TIMER_SLOT_OFF1, TIMER_SLOT_OFF2 };

inline bool valid_timer_slot(TimerSlot slot) { return static_cast<uint8_t>(slot) < 4; }

// Raw fields also support decoding captures; production operations keep both unknown flags set.
struct RkrTimerFields {
  uint16_t on_minutes{360};
  uint16_t off_minutes{1410};
  uint16_t clock_minutes{0};
  bool on_enabled{false};
  bool off_enabled{false};
  bool on_slot2{true};
  bool off_slot2{true};
  bool on_unknown_flag{true};
  bool off_unknown_flag{true};
};

inline bool valid_timer_minutes(uint16_t minutes) { return minutes < 1440 && minutes % 10 == 0; }

inline bool valid_timer_field(uint16_t minutes) {
  return valid_timer_minutes(minutes) || minutes == TIMER_TIME_SPECIAL;
}

/// Patch timer fields and command context; reject invalid values without modifying the frame.
inline bool encode_rkr_timers(const RkrTimerFields &timers, uint8_t (&frame)[STATE_LENGTH]) {
  if (!valid_timer_field(timers.on_minutes) || !valid_timer_field(timers.off_minutes) || timers.clock_minutes >= 1440)
    return false;
  frame[13] = (frame[13] & ~0x0E) | 0x08 | (timers.on_enabled ? 0x02 : 0) | (timers.off_enabled ? 0x04 : 0);
  frame[18] = timers.on_minutes & 0xFF;
  frame[19] = ((timers.on_minutes >> 8) & 7) | (timers.on_unknown_flag ? 8 : 0) | ((timers.off_minutes & 15) << 4);
  frame[20] = (timers.off_minutes >> 4) | (timers.off_unknown_flag ? 0x80 : 0);
  // Explicit timer context: hardware-tested OFF2 cancellation preserves OFF1 with 0x80, unlike 0x89.
  frame[23] = 0x80;
  frame[24] = timers.clock_minutes & 0xFF;
  frame[25] = (frame[25] & 0x38) | ((timers.clock_minutes >> 8) & 7) | (timers.on_slot2 ? 0x40 : 0) |
              (timers.off_slot2 ? 0x80 : 0);
  frame[26] = calc_checksum(frame, STATE_LENGTH);
  return true;
}

inline RkrTimerFields decode_rkr_timers(const uint8_t *frame) {
  return {.on_minutes = static_cast<uint16_t>(frame[18] | ((frame[19] & 7) << 8)),
          .off_minutes = static_cast<uint16_t>((frame[19] >> 4) | ((frame[20] & 0x7F) << 4)),
          .clock_minutes = static_cast<uint16_t>(frame[24] | ((frame[25] & 7) << 8)),
          .on_enabled = (frame[13] & 2) != 0,
          .off_enabled = (frame[13] & 4) != 0,
          .on_slot2 = (frame[25] & 0x40) != 0,
          .off_slot2 = (frame[25] & 0x80) != 0,
          .on_unknown_flag = (frame[19] & 8) != 0,
          .off_unknown_flag = (frame[20] & 0x80) != 0};
}

// Opposite-type selector stays at slot 1; enable + special time preserves its unknown state.
inline bool make_rkr_timer_operation(TimerSlot slot, uint16_t minutes, bool enabled, uint16_t clock_minutes,
                                     RkrTimerFields &fields) {
  if (!valid_timer_slot(slot) || !valid_timer_minutes(minutes) || clock_minutes >= 1440)
    return false;
  fields = RkrTimerFields{.on_minutes = TIMER_TIME_SPECIAL,
                          .off_minutes = TIMER_TIME_SPECIAL,
                          .clock_minutes = clock_minutes,
                          .on_enabled = true,
                          .off_enabled = true,
                          .on_slot2 = false,
                          .off_slot2 = false};
  if (slot == TimerSlot::TIMER_SLOT_ON1 || slot == TimerSlot::TIMER_SLOT_ON2) {
    fields.on_minutes = minutes;
    fields.on_enabled = enabled;
    fields.on_slot2 = slot == TimerSlot::TIMER_SLOT_ON2;
  } else {
    fields.off_minutes = minutes;
    fields.off_enabled = enabled;
    fields.off_slot2 = slot == TimerSlot::TIMER_SLOT_OFF2;
  }
  return true;
}

}  // namespace esphome::panasonic_ir
