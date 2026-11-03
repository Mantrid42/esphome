#pragma once

#include "panasonic_ac_frame.h"

namespace esphome::panasonic_ir {

// Slot selectors and packing match A75C4762 captures. Their effects on unselected slots need hardware testing.
struct RkrTimerFields {
  uint16_t on_minutes{360};
  uint16_t off_minutes{1410};
  uint16_t clock_minutes{0};
  bool on_enabled{false};
  bool off_enabled{false};
  bool on_slot2{true};
  bool off_slot2{true};
  bool update_on{true};
  bool update_off{true};
};

inline bool valid_timer_minutes(uint16_t minutes) { return minutes < 1440 && minutes % 10 == 0; }

/// Patch only timer fields of an existing full RKR frame. Reject invalid values without modifying it.
inline bool encode_rkr_timers(const RkrTimerFields &timers, uint8_t (&frame)[STATE_LENGTH]) {
  if (!valid_timer_minutes(timers.on_minutes) || !valid_timer_minutes(timers.off_minutes) ||
      timers.clock_minutes >= 1440)
    return false;
  frame[13] = (frame[13] & ~0x0E) | 0x08 | (timers.on_enabled ? 0x02 : 0) | (timers.off_enabled ? 0x04 : 0);
  frame[18] = timers.on_minutes & 0xFF;
  frame[19] = ((timers.on_minutes >> 8) & 7) | (timers.update_on ? 8 : 0) | ((timers.off_minutes & 15) << 4);
  frame[20] = (timers.off_minutes >> 4) | (timers.update_off ? 0x80 : 0);
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
          .update_on = (frame[19] & 8) != 0,
          .update_off = (frame[20] & 0x80) != 0};
}

}  // namespace esphome::panasonic_ir
