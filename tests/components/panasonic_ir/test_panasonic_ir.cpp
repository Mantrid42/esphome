#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

#include "esphome/components/panasonic_ir/panasonic_ir.h"
#include "esphome/components/panasonic_ir/panasonic_rkr_timers.h"
#include "esphome/components/panasonic_ir/timer_datetime.h"
#include "esphome/components/panasonic_ir/timer_switch.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

namespace esphome::panasonic_ir::testing {

using Frame = std::vector<uint8_t>;
using remote_base::RawTimings;

static constexpr uint32_t TOLERANCE_PERCENT = 25;

// ---- Real frames --------------------------------------------------------------------------------------------------

// From the IRremoteESP8266 unit tests
static const uint8_t JKE_COOL_25_OFF[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                          0x20, 0xE0, 0x04, 0x00, 0x30, 0x32, 0x80, 0xAF, 0x00,
                                          0x00, 0x06, 0x60, 0x00, 0x00, 0x80, 0x00, 0x06, 0x83};
static const uint8_t CKP_POWERFUL[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                       0x20, 0xE0, 0x04, 0x00, 0x4E, 0x2E, 0x80, 0xAF, 0x00,
                                       0x00, 0x0E, 0xE0, 0x11, 0x00, 0x01, 0x00, 0x06, 0xB7};
static const uint8_t CKP_QUIET[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x4E,
                                    0x2E, 0x80, 0xAF, 0x00, 0x00, 0x0E, 0xE0, 0x30, 0x00, 0x01, 0x00, 0x06, 0xD6};
static const uint8_t DKE_HEAT_25[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                      0x20, 0xE0, 0x04, 0x00, 0x4F, 0x32, 0x80, 0xAF, 0x0D,
                                      0x00, 0x06, 0x60, 0x00, 0x00, 0x01, 0x00, 0x06, 0x30};
// Captured from an A75C4762-family remote: fan, horizontal vane left, OFF timer at 00:20, clock at 10:10.
static const uint8_t RKR_FAN_TIMER_OFF_0020[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                                 0x20, 0xE0, 0x04, 0x00, 0x6D, 0x36, 0x80, 0x31, 0x0A,
                                                 0x68, 0x49, 0x81, 0x00, 0x00, 0x80, 0x62, 0x02, 0x7A};
// From the "Panasonic HVAC IR Protocol" notes by Mathieu (a CKP style remote)
static const uint8_t NOTES_CKP_HEAT_30[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                            0x20, 0xE0, 0x04, 0x00, 0x48, 0x3C, 0x80, 0xAF, 0x00,
                                            0x00, 0x0E, 0xE0, 0x10, 0x00, 0x01, 0x00, 0x06, 0xBE};
// From the Instructables article by the same author (a JKE style remote)
static const uint8_t ARTICLE_AUTO_25[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                          0x20, 0xE0, 0x04, 0x00, 0x01, 0x32, 0x80, 0xAF, 0x00,
                                          0x00, 0x06, 0x60, 0x00, 0x00, 0x80, 0x00, 0x06, 0x54};
// From the remote of a CS-NE9PKE. All heat, powered on, swing auto
static const uint8_t PKE_8C_FAN_MAX[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                         0x20, 0xE0, 0x04, 0x00, 0x49, 0x10, 0x80, 0x7F, 0x00,
                                         0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xCD};
// Frost protection at 10 C. The remote only offers 8 C and 10 C in this mode
static const uint8_t PKE_10C_FAN_MAX[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                          0x20, 0xE0, 0x04, 0x00, 0x49, 0x14, 0x80, 0x7F, 0x00,
                                          0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xD1};
// Cool at 24 C, fan auto, vane on fixed position 3 of 5 (1 is the highest)
static const uint8_t PKE_COOL_24C_VANE_3[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                              0x20, 0xE0, 0x04, 0x00, 0x39, 0x30, 0x80, 0xA3, 0x00,
                                              0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x01};
static const uint8_t PKE_16C_FAN_MAX[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                          0x20, 0xE0, 0x04, 0x00, 0x49, 0x20, 0x80, 0x7F, 0x00,
                                          0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xDD};
static const uint8_t PKE_30C_FAN_MAX[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                          0x20, 0xE0, 0x04, 0x00, 0x49, 0x3C, 0x80, 0x7F, 0x00,
                                          0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0xF9};
static const uint8_t PKE_19C_AUTO[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                       0x20, 0xE0, 0x04, 0x00, 0x49, 0x26, 0x80, 0xAF, 0x00,
                                       0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x13};
static const uint8_t PKE_20C_AUTO[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                       0x20, 0xE0, 0x04, 0x00, 0x49, 0x28, 0x80, 0xAF, 0x00,
                                       0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x15};
// The powerful/quiet toggle button of the remote of a CS-NE9PKE: the second section is only 8 bytes
static const uint8_t PKE_TOGGLE[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
                                     0x02, 0x20, 0xE0, 0x04, 0x80, 0x9C, 0x32, 0x54};

static Frame frame(const uint8_t (&bytes)[STATE_LENGTH]) { return Frame(bytes, bytes + STATE_LENGTH); }

// ---- Building captures --------------------------------------------------------------------------------------------

static void add_section(RawTimings &t, const uint8_t *bytes, size_t len) {
  t.push_back(HEADER_MARK);
  t.push_back(-static_cast<int32_t>(HEADER_SPACE));
  for (size_t i = 0; i < len; i++) {
    for (int bit = 0; bit < 8; bit++) {
      t.push_back(BIT_MARK);
      t.push_back(-static_cast<int32_t>((bytes[i] >> bit) & 1 ? BIT_ONE_SPACE : BIT_ZERO_SPACE));
    }
  }
  t.push_back(BIT_MARK);
}

enum class Sections : uint8_t { FIRST, SECOND, BOTH };

/// What a receiver captures. `idle` is the trailing space the receiver appends, as long as its idle time
static RawTimings capture(const uint8_t *state, Sections sections = Sections::BOTH, int idle = 0) {
  RawTimings t;
  if (sections != Sections::SECOND)
    add_section(t, state, SECTION1_LENGTH);
  if (sections == Sections::BOTH)
    t.push_back(-static_cast<int32_t>(SECTION_GAP));
  if (sections != Sections::FIRST)
    add_section(t, state + SECTION1_LENGTH, SECTION2_LENGTH);
  if (idle != 0)
    t.push_back(-idle);
  return t;
}

static bool read(const RawTimings &raw, uint8_t *state) {
  remote_base::RemoteReceiveData data(raw, TOLERANCE_PERCENT, remote_base::TOLERANCE_MODE_PERCENTAGE);
  return read_state(data, state);
}

// ---- Frame encoding and decoding ----------------------------------------------------------------------------------

TEST(PanasonicFrame, RkrTimerApplicationIsAnExplicitPerFrameChoice) {
  FrameParams params;
  params.model = Model::RKR;
  EXPECT_FALSE(params.apply_timers);
  for (const auto mode : {Mode::AUTO, Mode::COOL, Mode::HEAT, Mode::DRY, Mode::FAN}) {
    for (const bool power : {false, true}) {
      params.mode = mode;
      params.power = power;
      params.apply_timers = false;
      uint8_t preserved[STATE_LENGTH];
      encode_frame(params, preserved);
      EXPECT_EQ(preserved[13] & 0x08, 0);
      params.apply_timers = true;
      uint8_t applied[STATE_LENGTH];
      encode_frame(params, applied);
      EXPECT_EQ(applied[13], preserved[13] | 0x08);
      EXPECT_EQ(applied[26], static_cast<uint8_t>(preserved[26] + 8));
      for (size_t i = 0; i < STATE_LENGTH; i++) {
        if (i != 13 && i != 26)
          EXPECT_EQ(applied[i], preserved[i]) << i;
      }
      EXPECT_EQ(applied[26], calc_checksum(applied, STATE_LENGTH));
      params.apply_timers = false;
      encode_frame(params, applied);
      EXPECT_EQ(frame(applied), frame(preserved));
    }
  }
}

TEST(PanasonicFrame, TimerPolicyDoesNotChangeOtherModels) {
  for (const auto model : {Model::DKE, Model::JKE, Model::LKE, Model::NKE, Model::CKP, Model::PKE}) {
    FrameParams params;
    params.model = model;
    uint8_t preserved[STATE_LENGTH];
    encode_frame(params, preserved);
    params.apply_timers = true;
    uint8_t applied[STATE_LENGTH];
    encode_frame(params, applied);
    EXPECT_EQ(frame(applied), frame(preserved)) << static_cast<int>(model);
  }
}

TEST(PanasonicFrame, ShortCommandsMatchRemoteCaptures) {
  const uint8_t cancel_all[]{0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
                             0x02, 0x20, 0xE0, 0x04, 0x80, 0xB6, 0x32, 0x6E};
  uint8_t encoded[SHORT_COMMAND_LENGTH];
  encode_rkr_cancel_all_timers(encoded);
  EXPECT_EQ(Frame(encoded, encoded + sizeof(encoded)), Frame(cancel_all, cancel_all + sizeof(cancel_all)));
  EXPECT_EQ(encoded[15], sum_bytes(encoded + SECTION1_LENGTH, 7));
  const uint8_t toggle[]{0x80, 0x9C, 0x32};
  encode_short_command(toggle, encoded);
  EXPECT_EQ(Frame(encoded, encoded + sizeof(encoded)), Frame(PKE_TOGGLE, PKE_TOGGLE + sizeof(PKE_TOGGLE)));
}

TEST(PanasonicFrame, ShortCommandWaveformHasTwoEightByteSections) {
  uint8_t encoded[SHORT_COMMAND_LENGTH];
  encode_rkr_cancel_all_timers(encoded);
  remote_base::RemoteTransmitData data;
  encode_waveform(data, encoded);
  RawTimings expected;
  add_section(expected, encoded, 8);
  expected.push_back(-static_cast<int32_t>(SECTION_GAP));
  add_section(expected, encoded + 8, 8);
  EXPECT_EQ(data.get_carrier_frequency(), 36700);
  EXPECT_EQ(data.get_data().size(), 263);
  EXPECT_EQ(data.get_data(), expected);
  uint8_t state[STATE_LENGTH];
  EXPECT_FALSE(read(data.get_data(), state));  // A command must not be published as climate state.
}

TEST(PanasonicFrame, StateWaveformRemainsUnchangedForEveryModel) {
  for (const auto model : {Model::DKE, Model::JKE, Model::LKE, Model::NKE, Model::CKP, Model::RKR, Model::PKE}) {
    FrameParams params;
    params.model = model;
    uint8_t state[STATE_LENGTH];
    encode_frame(params, state);
    remote_base::RemoteTransmitData data;
    encode_waveform(data, state);
    EXPECT_EQ(data.get_carrier_frequency(), 36700);
    EXPECT_EQ(data.get_data().size(), 439);
    EXPECT_EQ(data.get_data(), capture(state));
  }
}

TEST(PanasonicFrame, ChecksumMatchesEveryRealFrame) {
  for (const auto *f :
       {JKE_COOL_25_OFF, CKP_POWERFUL, CKP_QUIET, DKE_HEAT_25, NOTES_CKP_HEAT_30, ARTICLE_AUTO_25, PKE_8C_FAN_MAX,
        PKE_16C_FAN_MAX, PKE_30C_FAN_MAX, PKE_19C_AUTO, PKE_20C_AUTO, RKR_FAN_TIMER_OFF_0020}) {
    EXPECT_EQ(calc_checksum(f, STATE_LENGTH), f[STATE_LENGTH - 1]);
  }
}

TEST(PanasonicFrame, ChecksumSeedCancelsTheFirstSection) {
  // The first section is constant, and sums to 0x10C. So the 0xF4 seed makes the checksum a plain sum of the second one
  uint8_t tmpl[STATE_LENGTH];
  fill_template(tmpl);
  EXPECT_EQ(sum_bytes(tmpl, SECTION1_LENGTH, CHECKSUM_INIT), 0);
  EXPECT_EQ(sum_bytes(NOTES_CKP_HEAT_30 + SECTION1_LENGTH, SECTION2_LENGTH - 1), NOTES_CKP_HEAT_30[STATE_LENGTH - 1]);
}

TEST(PanasonicFrame, EncodesTheAuthorsOwnFramesExactly) {
  struct Case {
    const uint8_t (&expected)[STATE_LENGTH];
    uint8_t temperature;
    uint8_t fan;
  };
  const Case cases[] = {{PKE_16C_FAN_MAX, 16, FAN_SPEED_MAX},
                        {PKE_30C_FAN_MAX, 30, FAN_SPEED_MAX},
                        {PKE_19C_AUTO, 19, FAN_SPEED_AUTO},
                        {PKE_20C_AUTO, 20, FAN_SPEED_AUTO}};
  for (const Case &c : cases) {
    SCOPED_TRACE(static_cast<int>(c.temperature));
    FrameParams params;
    params.model = Model::PKE;
    params.power = true;
    params.mode = Mode::HEAT;
    params.temperature = c.temperature;
    params.fan_speed = c.fan;
    uint8_t state[STATE_LENGTH];
    encode_frame(params, state);
    EXPECT_EQ(frame(state), frame(c.expected));
  }
}

TEST(PanasonicFrame, EncodesFrostProtectionAsTheRemoteDoes) {
  FrameParams params;
  params.model = Model::PKE;
  params.power = true;
  params.mode = Mode::HEAT;
  params.temperature = TEMP_MAINTENANCE;
  params.fan_speed = FAN_SPEED_MAX;
  uint8_t state[STATE_LENGTH];
  encode_frame(params, state);
  EXPECT_EQ(frame(state), frame(PKE_10C_FAN_MAX));

  // Switching the unit off keeps the power bit clear
  params.power = false;
  encode_frame(params, state);
  EXPECT_EQ(state[13] & 0x01, 0);
}

TEST(PanasonicFrame, PkeIsJkeWithBit3SetInByte13) {
  FrameParams params;
  params.power = true;
  params.mode = Mode::HEAT;
  uint8_t jke[STATE_LENGTH], pke[STATE_LENGTH];
  params.model = Model::JKE;
  encode_frame(params, jke);
  params.model = Model::PKE;
  encode_frame(params, pke);
  EXPECT_EQ(jke[13], 0x41);
  EXPECT_EQ(pke[13], 0x49);
}

TEST(PanasonicFrame, EncodesTheCkpModelBytes) {
  FrameParams params;
  params.model = Model::CKP;
  params.mode = Mode::HEAT;
  params.temperature = 30;
  uint8_t state[STATE_LENGTH];
  encode_frame(params, state);
  EXPECT_EQ(state[21], NOTES_CKP_HEAT_30[21]);  // b4 is always set
  EXPECT_EQ(state[23], NOTES_CKP_HEAT_30[23]);
}

TEST(PanasonicFrame, EncodesHorizontalVaneOnlyForSupportedModels) {
  for (Model model : {Model::DKE, Model::RKR}) {
    FrameParams p;
    p.model = model;
    for (uint8_t position :
         {VANE_H_AUTO, VANE_H_FULL_LEFT, VANE_H_LEFT, VANE_H_MIDDLE, VANE_H_RIGHT, VANE_H_FULL_RIGHT}) {
      p.vane_h = position;
      uint8_t state[STATE_LENGTH];
      encode_frame(p, state);
      EXPECT_EQ(state[17], position);
      FrameParams decoded;
      decoded.model = model;
      ASSERT_TRUE(decode_frame(state, decoded));
      EXPECT_EQ(decoded.vane_h, position);
    }
  }

  // Other models have a fixed byte 17 and must not be affected by a horizontal vane request.
  for (Model model : {Model::JKE, Model::LKE, Model::NKE, Model::CKP, Model::PKE}) {
    FrameParams p;
    p.model = model;
    uint8_t original[STATE_LENGTH];
    encode_frame(p, original);
    p.vane_h = VANE_H_FULL_RIGHT;
    uint8_t changed[STATE_LENGTH];
    encode_frame(p, changed);
    EXPECT_EQ(frame(original), frame(changed));
  }
}

TEST(PanasonicFrame, FanOnlyAlwaysSends27C) {
  FrameParams params;
  uint8_t state[STATE_LENGTH];
  params.mode = Mode::FAN;
  params.temperature = 18;
  encode_frame(params, state);
  EXPECT_EQ((state[14] >> 1) & 0x1F, 27);
}

TEST(PanasonicFrame, DecodesRealFrames) {
  FrameParams p;
  ASSERT_TRUE(decode_frame(JKE_COOL_25_OFF, p));
  EXPECT_FALSE(p.power);
  EXPECT_EQ(p.mode, Mode::COOL);
  EXPECT_EQ(p.temperature, 25);
  EXPECT_EQ(p.fan_speed, FAN_SPEED_AUTO);
  EXPECT_EQ(p.vane_v, VANE_V_AUTO);

  p = {};
  ASSERT_TRUE(decode_frame(ARTICLE_AUTO_25, p));
  EXPECT_TRUE(p.power);
  EXPECT_EQ(p.mode, Mode::AUTO);
  EXPECT_EQ(p.temperature, 25);

  p = {};
  ASSERT_TRUE(decode_frame(NOTES_CKP_HEAT_30, p));
  EXPECT_EQ(p.mode, Mode::HEAT);
  EXPECT_EQ(p.temperature, 30);

  // Quiet and powerful are not used, but the frames are still valid states
  for (const auto *f : {CKP_POWERFUL, CKP_QUIET}) {
    p = {};
    ASSERT_TRUE(decode_frame(f, p));
    EXPECT_EQ(p.mode, Mode::HEAT);
    EXPECT_EQ(p.temperature, 23);
  }

  p = {};
  ASSERT_TRUE(decode_frame(DKE_HEAT_25, p));
  EXPECT_TRUE(p.power);
  EXPECT_EQ(p.mode, Mode::HEAT);
  EXPECT_EQ(p.temperature, 25);
}

TEST(PanasonicFrame, DecodesHorizontalVaneFromCapturedRkrRemote) {
  FrameParams p;
  p.model = Model::RKR;
  ASSERT_TRUE(decode_frame(RKR_FAN_TIMER_OFF_0020, p));
  EXPECT_EQ(p.vane_h, VANE_H_LEFT);
}

TEST(PanasonicFrame, DecodesTheAuthorsFrostProtectionFrames) {
  for (const auto &c : {std::make_pair(PKE_10C_FAN_MAX, 10), std::make_pair(PKE_8C_FAN_MAX, 8)}) {
    FrameParams p;
    ASSERT_TRUE(decode_frame(c.first, p));
    EXPECT_TRUE(p.power);
    EXPECT_EQ(p.mode, Mode::HEAT);
    EXPECT_EQ(p.temperature, c.second);
    EXPECT_EQ(p.fan_speed, FAN_SPEED_MAX);
  }

  // Ordinary frames are never mistaken for it
  for (const auto *f : {PKE_16C_FAN_MAX, PKE_30C_FAN_MAX, PKE_19C_AUTO, PKE_20C_AUTO}) {
    FrameParams n;
    ASSERT_TRUE(decode_frame(f, n));
    EXPECT_GE(n.temperature, TEMP_MIN);
  }
}

TEST(PanasonicFrame, RejectsInvalidFrames) {
  // Sets one byte of a real frame and fixes up the checksum, so only that byte is wrong
  auto corrupt = [](const uint8_t *real, size_t index, uint8_t value) {
    FrameParams p;
    uint8_t bad[STATE_LENGTH];
    memcpy(bad, real, STATE_LENGTH);
    bad[index] = value;
    bad[STATE_LENGTH - 1] = calc_checksum(bad, STATE_LENGTH);
    return decode_frame(bad, p);
  };

  FrameParams p;
  uint8_t bad[STATE_LENGTH];
  memcpy(bad, JKE_COOL_25_OFF, STATE_LENGTH);
  bad[26] ^= 1;  // Checksum
  EXPECT_FALSE(decode_frame(bad, p));

  EXPECT_FALSE(corrupt(JKE_COOL_25_OFF, 3, 0));      // Static prefix
  EXPECT_FALSE(corrupt(JKE_COOL_25_OFF, 13, 0x10));  // Mode 1 doesn't exist
  EXPECT_FALSE(corrupt(PKE_8C_FAN_MAX, 14, 0x0E));   // 7 C, below even the frost protection frame
  EXPECT_FALSE(corrupt(JKE_COOL_25_OFF, 16, 0x0F));  // Fan nibble 0
}

TEST(PanasonicFrame, RoundTripsEverythingTheEncoderCanProduce) {
  auto round_trip = [](const FrameParams &in) {
    uint8_t state[STATE_LENGTH];
    encode_frame(in, state);
    FrameParams out;
    ASSERT_TRUE(decode_frame(state, out));
    EXPECT_EQ(out.power, in.power);
    EXPECT_EQ(out.mode, in.mode);
    EXPECT_EQ(out.temperature, in.mode == Mode::FAN ? TEMP_FAN_MODE : in.temperature);
    EXPECT_EQ(out.fan_speed, in.fan_speed);
    EXPECT_EQ(out.vane_v, in.vane_v);
  };
  // Each setting is varied on its own against a baseline, for every model
  for (auto model : {Model::DKE, Model::JKE, Model::LKE, Model::NKE, Model::CKP, Model::RKR, Model::PKE}) {
    SCOPED_TRACE(static_cast<int>(model));
    FrameParams base;
    base.model = model;
    base.mode = Mode::COOL;
    for (bool power : {false, true}) {
      FrameParams p = base;
      p.power = power;
      round_trip(p);
    }
    for (auto mode : {Mode::AUTO, Mode::DRY, Mode::COOL, Mode::HEAT, Mode::FAN}) {
      FrameParams p = base;
      p.mode = mode;
      round_trip(p);
    }
    for (uint8_t fan :
         {FAN_SPEED_MIN, FAN_SPEED_LOW, FAN_SPEED_MEDIUM, FAN_SPEED_HIGH, FAN_SPEED_MAX, FAN_SPEED_AUTO}) {
      FrameParams p = base;
      p.fan_speed = fan;
      round_trip(p);
    }
    for (uint8_t temp = TEMP_MIN_RECEIVE; temp <= TEMP_MAX; temp++) {
      FrameParams p = base;
      p.temperature = temp;
      round_trip(p);
    }
    for (uint8_t vane : {VANE_V_HIGHEST, uint8_t{2}, VANE_V_MIDDLE, uint8_t{4}, VANE_V_LOWEST, VANE_V_AUTO}) {
      FrameParams p = base;
      p.vane_v = vane;
      round_trip(p);
    }
  }
}

// ---- Reading a capture --------------------------------------------------------------------------------------------

TEST(PanasonicReceive, WholeFrameForAnyIdleTime) {
  for (int idle : {0, 5000, 10000, 20000, 30000}) {
    SCOPED_TRACE(idle);
    uint8_t state[STATE_LENGTH] = {};
    ASSERT_TRUE(read(capture(PKE_20C_AUTO, Sections::BOTH, idle), state));
    EXPECT_EQ(frame(state), frame(PKE_20C_AUTO));
  }
}

TEST(PanasonicReceive, SplitCapturesNeedOnlyTheSecondSection) {
  // The 10 ms gap is as long as the default idle time, and a shorter idle time always splits the frame
  for (int idle : {0, 5000, 10000}) {
    SCOPED_TRACE(idle);
    uint8_t state[STATE_LENGTH] = {};
    EXPECT_FALSE(read(capture(PKE_20C_AUTO, Sections::FIRST, idle), state));
    ASSERT_TRUE(read(capture(PKE_20C_AUTO, Sections::SECOND, idle), state));
    EXPECT_EQ(frame(state), frame(PKE_20C_AUTO));
  }
}

TEST(PanasonicReceive, IgnoresShortButtonFrames) {
  // Powerful/quiet and nanoe-G have an 8 byte second section of their own
  uint8_t state[STATE_LENGTH] = {};
  RawTimings whole;
  add_section(whole, PKE_TOGGLE, 8);
  whole.push_back(-static_cast<int32_t>(SECTION_GAP));
  add_section(whole, PKE_TOGGLE + 8, 8);
  EXPECT_FALSE(read(whole, state));

  RawTimings lone;
  add_section(lone, PKE_TOGGLE + 8, 8);
  lone.push_back(-5000);
  EXPECT_FALSE(read(lone, state));
}

TEST(PanasonicReceive, RejectsBrokenCaptures) {
  uint8_t state[STATE_LENGTH] = {};
  EXPECT_FALSE(read({}, state));

  RawTimings truncated = capture(PKE_20C_AUTO);
  truncated.resize(truncated.size() - 40);
  EXPECT_FALSE(read(truncated, state));

  RawTimings bad_bit = capture(PKE_20C_AUTO);
  bad_bit[5] = 9000;
  EXPECT_FALSE(read(bad_bit, state));

  // Too many bytes must be rejected without writing past the buffer
  RawTimings too_long;
  const uint8_t zeros[40] = {};
  add_section(too_long, zeros, sizeof(zeros));
  EXPECT_FALSE(read(too_long, state));

  RawTimings junk = capture(PKE_20C_AUTO, Sections::FIRST);
  junk.push_back(-3000);
  junk.push_back(HEADER_MARK);
  EXPECT_FALSE(read(junk, state));

  uint8_t other[STATE_LENGTH];
  memcpy(other, PKE_20C_AUTO, STATE_LENGTH);
  other[0] = 0x12;  // Not our first section
  EXPECT_FALSE(read(capture(other), state));
}

TEST(PanasonicReceive, ToleratesTimingDrift) {
  for (int percent : {90, 110}) {
    SCOPED_TRACE(percent);
    RawTimings raw = capture(PKE_20C_AUTO);
    for (auto &v : raw)
      v = v * percent / 100;
    uint8_t state[STATE_LENGTH] = {};
    ASSERT_TRUE(read(raw, state));
    EXPECT_EQ(frame(state), frame(PKE_20C_AUTO));
  }
}

// ---- The climate entity -------------------------------------------------------------------------------------------

class CapturingTransmitter : public remote_base::RemoteTransmitterBase {
 public:
  CapturingTransmitter() : RemoteTransmitterBase(nullptr) {}
  std::vector<RawTimings> frames;

 protected:
  void send_internal(uint32_t send_times, uint32_t send_wait) override {
    this->frames.push_back(this->temp_.get_data());
  }
};

struct Fixture {
  CapturingTransmitter transmitter;
  PanasonicIrClimate climate;

  /// With `restore`, setup() restores the state an earlier fixture in the same test saved
  explicit Fixture(Model model = Model::PKE, bool restore = false) {
    if (!restore && global_preferences != nullptr)
      global_preferences->reset();
    // The test main does not construct App as generated code does
    static const bool APP_CONSTRUCTED = (new (&App) Application(), true);
    (void) APP_CONSTRUCTED;
    static char name[] = "test_panasonic_ir";
    static char friendly_name[] = "";
    App.pre_setup(name, sizeof(name) - 1, friendly_name, 0);
    this->climate.set_model(model);
    this->climate.set_transmitter(&this->transmitter);
    static_cast<Component &>(this->climate).setup();
  }

  /// Builds a call, lets `fill` set it up, and performs it
  template<typename F> void command(F &&fill) {
    auto call = this->climate.make_call();
    fill(call);
    call.perform();
  }

  /// The state of the last transmission, read back with the same code that reads a remote
  Frame last_state() const {
    EXPECT_FALSE(this->transmitter.frames.empty());
    if (this->transmitter.frames.empty())
      return {};
    uint8_t state[STATE_LENGTH] = {};
    EXPECT_TRUE(read(this->transmitter.frames.back(), state));
    return frame(state);
  }

  bool receive(const RawTimings &raw) {
    remote_base::RemoteReceiveData data(raw, TOLERANCE_PERCENT, remote_base::TOLERANCE_MODE_PERCENTAGE);
    // The receiver calls through the listener interface, where on_receive is public
    return static_cast<remote_base::RemoteReceiverListener &>(this->climate).on_receive(data);
  }
  bool receive(const uint8_t (&state)[STATE_LENGTH]) { return this->receive(capture(state)); }
};

TEST(PanasonicClimate, TransmitsTheAuthorsFramesExactly) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_HEAT);
    call.set_target_temperature(20.0f);
    call.set_fan_mode(climate::CLIMATE_FAN_AUTO);
    call.set_swing_mode(climate::CLIMATE_SWING_VERTICAL);  // The remote sends the vane on auto
  });
  EXPECT_EQ(f.last_state(), frame(PKE_20C_AUTO));

  f.command([](auto &call) { call.set_target_temperature(19.0f); });
  EXPECT_EQ(f.last_state(), frame(PKE_19C_AUTO));

  f.command([](auto &call) {
    call.set_target_temperature(30.0f);
    call.set_fan_mode("Maximum");
  });
  EXPECT_EQ(f.last_state(), frame(PKE_30C_FAN_MAX));
}

TEST(PanasonicClimate, TransmitsTwoSectionsWithTheRightTimings) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(24.0f);
  });
  ASSERT_EQ(f.transmitter.frames.size(), 1u);
  const RawTimings &t = f.transmitter.frames.back();
  // header + 8 bytes + footer, gap, header + 19 bytes + footer
  ASSERT_EQ(t.size(), static_cast<size_t>(2 + 8 * 16 + 1 + 1 + 2 + 19 * 16 + 1));
  EXPECT_EQ(t[0], static_cast<int32_t>(HEADER_MARK));
  EXPECT_EQ(t[1], -static_cast<int32_t>(HEADER_SPACE));
  EXPECT_EQ(t[2 + 8 * 16], static_cast<int32_t>(BIT_MARK));
  EXPECT_EQ(t[2 + 8 * 16 + 1], -static_cast<int32_t>(SECTION_GAP));
  EXPECT_EQ(t.back(), static_cast<int32_t>(BIT_MARK));
}

TEST(PanasonicClimate, RkrPreservesTimersAcrossFanAndPowerChanges) {
  // Replayed on an A75C4762-family unit: both frames were accepted with the timer indicator still lit.
  static constexpr uint8_t RKR_FAN_ON[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                           0x20, 0xE0, 0x04, 0x00, 0x61, 0x36, 0x80, 0x41, 0x06,
                                           0x00, 0x0E, 0xE0, 0x00, 0x00, 0x89, 0x00, 0x00, 0xDB};
  static constexpr uint8_t RKR_FAN_OFF[] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02,
                                            0x20, 0xE0, 0x04, 0x00, 0x60, 0x36, 0x80, 0x41, 0x06,
                                            0x00, 0x0E, 0xE0, 0x00, 0x00, 0x89, 0x00, 0x00, 0xDA};
  Fixture f(Model::RKR);
  f.climate.set_vertical_vane_default(1);
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_FAN_ONLY);
    call.set_fan_mode("Minimum");
  });
  f.command([](auto &call) { call.set_fan_mode(climate::CLIMATE_FAN_LOW); });
  EXPECT_EQ(f.last_state(), frame(RKR_FAN_ON));
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });
  EXPECT_EQ(f.last_state(), frame(RKR_FAN_OFF));
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_FAN_ONLY); });
  EXPECT_EQ(f.last_state(), frame(RKR_FAN_ON));
}

TEST(PanasonicClimate, OffKeepsTheLastModeAndClearsThePowerBit) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(23.0f);
  });
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });

  FrameParams p;
  const Frame last = f.last_state();
  ASSERT_TRUE(decode_frame(last.data(), p));
  EXPECT_FALSE(p.power);
  EXPECT_EQ(p.mode, Mode::COOL);
  EXPECT_EQ(p.temperature, 23);
}

TEST(PanasonicClimate, CkpOnlyHasAPowerToggle) {
  Fixture f(Model::CKP);
  auto power_bit = [&]() { return f.last_state()[13] & 0x01; };

  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_HEAT);
    call.set_target_temperature(22.0f);
  });
  EXPECT_EQ(power_bit(), 1);  // Off -> on toggles

  f.command([](auto &call) { call.set_target_temperature(23.0f); });
  EXPECT_EQ(power_bit(), 0);  // Already on, so changing a setting must not toggle it off

  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });
  EXPECT_EQ(power_bit(), 1);  // On -> off toggles

  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });
  EXPECT_EQ(power_bit(), 0);  // Already off
}

TEST(PanasonicClimate, CkpRestoredOnDoesNotToggleOff) {
  {
    Fixture f(Model::CKP);
    f.command([](auto &call) {
      call.set_mode(climate::CLIMATE_MODE_HEAT);
      call.set_target_temperature(22.0f);
    });
  }
  Fixture f(Model::CKP, true);
  ASSERT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT);
  f.command([](auto &call) { call.set_target_temperature(23.0f); });
  EXPECT_EQ(f.last_state()[13] & 0x01, 0);  // The unit is taken to be on still
}

TEST(PanasonicClimate, FrostProtectionIsAPreset) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(24.0f);
  });

  f.command([](auto &call) { call.set_preset(climate::CLIMATE_PRESET_AWAY); });
  EXPECT_EQ(f.last_state(), frame(PKE_10C_FAN_MAX));
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT);
  EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_AWAY);
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 24.0f);  // The set-point is not touched

  // Whatever else the call sets, the frame is the remote's
  f.command([](auto &call) {
    call.set_preset(climate::CLIMATE_PRESET_AWAY);
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_fan_mode(climate::CLIMATE_FAN_LOW);
  });
  EXPECT_EQ(f.last_state(), frame(PKE_10C_FAN_MAX));
  // and the entity shows what was sent
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT);
  EXPECT_EQ(f.climate.get_custom_fan_mode(), "Maximum");
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_VERTICAL);

  // Changing anything else takes the unit out of it
  f.command([](auto &call) { call.set_target_temperature(21.0f); });
  EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_NONE);
  FrameParams p;
  const Frame last = f.last_state();
  ASSERT_TRUE(decode_frame(last.data(), p));
  EXPECT_EQ(p.temperature, 21);
}

TEST(PanasonicClimate, PresetNoneLeavesFrostProtection) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(24.0f);
  });
  f.command([](auto &call) { call.set_preset(climate::CLIMATE_PRESET_AWAY); });
  ASSERT_EQ(f.last_state(), frame(PKE_10C_FAN_MAX));

  f.command([](auto &call) { call.set_preset(climate::CLIMATE_PRESET_NONE); });
  EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_NONE);
  FrameParams p;
  const Frame last = f.last_state();
  ASSERT_TRUE(decode_frame(last.data(), p));
  EXPECT_EQ(p.temperature, 24);
}

TEST(PanasonicClimate, CkpIgnoresTheEchoOfItsOwnToggle) {
  Fixture f(Model::CKP);
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_HEAT);
    call.set_target_temperature(22.0f);
  });
  const RawTimings sent = f.transmitter.frames.back();

  ASSERT_TRUE(f.receive(sent));  // Our own toggle, heard back
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT);

  ASSERT_TRUE(f.receive(sent));  // The same frame again is a press of the remote
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_OFF);
}

TEST(PanasonicClimate, EchoKeepsTheVaneFollowingTheMode) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(24.0f);
    call.set_swing_mode(climate::CLIMATE_SWING_OFF);
  });
  ASSERT_EQ(f.last_state()[16] & 0x0F, VANE_V_HIGHEST);
  ASSERT_TRUE(f.receive(f.transmitter.frames.back()));  // Our own frame, heard back

  // No position was chosen, so heating still blows down
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_HEAT); });
  EXPECT_EQ(f.last_state()[16] & 0x0F, VANE_V_LOWEST);
}

TEST(PanasonicClimate, RemoteFrameWithTheEchoChecksumIsApplied) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_HEAT);
    call.set_target_temperature(22.0f);
    call.set_swing_mode(climate::CLIMATE_SWING_OFF);
  });
  const Frame sent = f.last_state();
  ASSERT_EQ(sent[16] & 0x0F, VANE_V_LOWEST);

  // One degree warmer (B14 + 2) and the vane two steps up (B16 - 2) keeps the checksum
  uint8_t remote[STATE_LENGTH];
  memcpy(remote, sent.data(), STATE_LENGTH);
  remote[14] += 2;
  remote[16] -= 2;
  ASSERT_EQ(remote[STATE_LENGTH - 1], calc_checksum(remote, STATE_LENGTH));

  ASSERT_TRUE(f.receive(remote));
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 23.0f);
}

TEST(PanasonicClimate, ReceivesTheRemote) {
  Fixture f;
  ASSERT_TRUE(f.receive(PKE_20C_AUTO));
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT);
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 20.0f);
  EXPECT_EQ(f.climate.fan_mode, climate::CLIMATE_FAN_AUTO);
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_VERTICAL);
  EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_NONE);

  ASSERT_TRUE(f.receive(PKE_30C_FAN_MAX));
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 30.0f);
  EXPECT_TRUE(f.climate.has_custom_fan_mode());
  EXPECT_EQ(f.climate.get_custom_fan_mode(), "Maximum");

  ASSERT_TRUE(f.receive(JKE_COOL_25_OFF));  // Power bit clear
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_OFF);
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 25.0f);
  EXPECT_FALSE(f.climate.has_custom_fan_mode());
  EXPECT_EQ(f.climate.fan_mode, climate::CLIMATE_FAN_AUTO);
}

TEST(PanasonicClimate, ReceivesFrostProtectionWithoutLosingTheSetPoint) {
  Fixture f;
  ASSERT_TRUE(f.receive(PKE_19C_AUTO));
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 19.0f);

  // The remote sends 8 C or 10 C in this mode
  for (const auto *frost : {PKE_10C_FAN_MAX, PKE_8C_FAN_MAX}) {
    ASSERT_TRUE(f.receive(capture(frost)));
    EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_AWAY);
    EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT);
    EXPECT_FLOAT_EQ(f.climate.target_temperature, 19.0f);  // Frost protection is a mode, not a set-point
    EXPECT_EQ(f.climate.get_custom_fan_mode(), "Maximum");
  }

  ASSERT_TRUE(f.receive(PKE_20C_AUTO));
  EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_NONE);
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 20.0f);
}

TEST(PanasonicClimate, SwingVerticalIsVaneAutoAndOffIsAFixedPosition) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(24.0f);
    call.set_swing_mode(climate::CLIMATE_SWING_VERTICAL);
  });
  EXPECT_EQ(f.last_state()[16] & 0x0F, VANE_V_AUTO);

  // Until a position is chosen, cooling blows up and heating blows down
  f.command([](auto &call) { call.set_swing_mode(climate::CLIMATE_SWING_OFF); });
  EXPECT_EQ(f.last_state()[16] & 0x0F, VANE_V_HIGHEST);
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_HEAT); });
  EXPECT_EQ(f.last_state()[16] & 0x0F, VANE_V_LOWEST);
}

TEST(PanasonicClimate, VanePositionSelectsAFixedPosition) {
  Fixture f;
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(24.0f);
    call.set_fan_mode(climate::CLIMATE_FAN_AUTO);
  });

  f.climate.set_vane_position(3);
  EXPECT_EQ(f.last_state(), frame(PKE_COOL_24C_VANE_3));
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_OFF);

  // The position is kept when the mode changes
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_HEAT); });
  EXPECT_EQ(f.last_state()[16] & 0x0F, 3);

  for (size_t position = 1; position <= 5; position++) {
    f.climate.set_vane_position(position);
    EXPECT_EQ(f.last_state()[16] & 0x0F, static_cast<int>(position));
  }

  f.climate.set_vane_position(0);
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_VERTICAL);
  EXPECT_EQ(f.last_state()[16] & 0x0F, VANE_V_AUTO);
}

TEST(PanasonicClimate, VanePositionOutOfRangeIsIgnored) {
  Fixture f;
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_COOL); });
  const size_t sent = f.transmitter.frames.size();
  f.climate.set_vane_position(6);
  EXPECT_EQ(f.transmitter.frames.size(), sent);
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_OFF);
}

TEST(PanasonicClimate, VanePositionLeavesFrostProtection) {
  Fixture f;
  f.command([](auto &call) { call.set_preset(climate::CLIMATE_PRESET_AWAY); });
  f.climate.set_vane_position(3);
  EXPECT_EQ(f.climate.preset, climate::CLIMATE_PRESET_NONE);
  EXPECT_EQ(f.last_state()[16] & 0x0F, 3);
}

TEST(PanasonicClimate, HorizontalVaneSelectKeepsPositionAcrossCommands) {
  Fixture f(Model::RKR);
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(23.0f);
  });
  EXPECT_EQ(f.last_state()[17], VANE_H_MIDDLE);

  for (size_t i = 0; i < 6; i++) {
    f.climate.set_horizontal_vane_position(i);
    EXPECT_EQ(f.last_state()[17], HORIZONTAL_VANE_POSITIONS[i]);
  }

  f.command([](auto &call) { call.set_target_temperature(24.0f); });
  EXPECT_EQ(f.last_state()[17], VANE_H_FULL_RIGHT);

  const size_t sent = f.transmitter.frames.size();
  f.climate.set_horizontal_vane_position(6);
  EXPECT_EQ(f.transmitter.frames.size(), sent);
}

TEST(PanasonicClimate, VerticalDefaultIsKeptUntilSelectedOtherwise) {
  Fixture f(Model::DKE);
  f.climate.set_vertical_vane_default(4);
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_COOL); });
  EXPECT_EQ(f.last_state()[16] & 0x0F, 4);

  f.climate.set_vane_position(2);
  EXPECT_EQ(f.last_state()[16] & 0x0F, 2);
}

TEST(PanasonicClimate, HorizontalVaneTracksRemoteCommands) {
  Fixture f(Model::RKR);
  ASSERT_TRUE(f.receive(RKR_FAN_TIMER_OFF_0020));
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_COOL); });
  EXPECT_EQ(f.last_state()[17], VANE_H_LEFT);
}

TEST(PanasonicClimate, UnsupportedModelIgnoresHorizontalVaneChanges) {
  Fixture f(Model::PKE);
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_COOL); });
  const size_t sent = f.transmitter.frames.size();
  f.climate.set_horizontal_vane_position(1);
  EXPECT_EQ(f.transmitter.frames.size(), sent);
}

TEST(PanasonicClimate, ReceivesTheVanePosition) {
  Fixture f;
  ASSERT_TRUE(f.receive(PKE_COOL_24C_VANE_3));
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_OFF);

  // Swing off keeps sending that position
  f.command([](auto &call) { call.set_target_temperature(22.0f); });
  EXPECT_EQ(f.last_state()[16] & 0x0F, 3);

  ASSERT_TRUE(f.receive(PKE_20C_AUTO));  // Vane auto
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_VERTICAL);

  // A vane value the remote never sends leaves the vane as it was
  FrameParams p;
  ASSERT_TRUE(decode_frame(frame(PKE_COOL_24C_VANE_3).data(), p));
  p.vane_v = 9;
  uint8_t odd[STATE_LENGTH];
  encode_frame(p, odd);
  ASSERT_TRUE(f.receive(odd));
  EXPECT_EQ(f.climate.swing_mode, climate::CLIMATE_SWING_VERTICAL);
}

TEST(PanasonicClimate, ReceivesSplitCaptures) {
  Fixture f;
  EXPECT_FALSE(f.receive(capture(PKE_20C_AUTO, Sections::FIRST, 5000)));
  ASSERT_TRUE(f.receive(capture(PKE_20C_AUTO, Sections::SECOND, 5000)));
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 20.0f);
}

TEST(PanasonicClimate, IgnoresWhatItDoesNotUnderstand) {
  Fixture f;
  ASSERT_TRUE(f.receive(PKE_20C_AUTO));

  uint8_t bad[STATE_LENGTH];
  memcpy(bad, PKE_30C_FAN_MAX, STATE_LENGTH);
  bad[26] ^= 1;
  EXPECT_FALSE(f.receive(bad));
  EXPECT_FLOAT_EQ(f.climate.target_temperature, 20.0f);  // Unchanged
}

TEST(PanasonicClimate, HearsItsOwnTransmissions) {
  // Whatever the entity sends must read back to the state it was set to, for every model
  for (auto model : {Model::DKE, Model::JKE, Model::LKE, Model::NKE, Model::CKP, Model::RKR, Model::PKE}) {
    SCOPED_TRACE(static_cast<int>(model));
    Fixture f(model);
    f.command([](auto &call) {
      call.set_mode(climate::CLIMATE_MODE_COOL);
      call.set_target_temperature(22.0f);
      call.set_fan_mode("Minimum");
    });

    Fixture g(model);
    ASSERT_TRUE(g.receive(f.transmitter.frames.back()));
    EXPECT_EQ(g.climate.mode, climate::CLIMATE_MODE_COOL);
    EXPECT_FLOAT_EQ(g.climate.target_temperature, 22.0f);
    EXPECT_EQ(g.climate.get_custom_fan_mode(), "Minimum");
  }
}

static Command decode_command(const RawTimings &raw) {
  remote_base::RemoteReceiveData data(raw, TOLERANCE_PERCENT, remote_base::TOLERANCE_MODE_PERCENTAGE);
  return read_command(data);
}

static RawTimings command_capture(const Command &command) {
  RawTimings raw;
  add_section(raw, command.bytes, 8);
  raw.push_back(-static_cast<int32_t>(SECTION_GAP));
  add_section(raw, command.bytes + 8, command.length - 8);
  return raw;
}

TEST(PanasonicProtocol, HexFormsRegenerateChecksums) {
  for (const char *input : {"02 20 E0 04 00 00 00 06 02 20 E0 04 80 B6 32 00", "0220e004000000060220e00480b632",
                            "02 20 E0 04 80 B6 32 00", "\t0220E00480B632\n"}) {
    Command command;
    ASSERT_TRUE(parse_command_hex(StringRef(input), command)) << input;
    EXPECT_EQ(command.length, 16);
    EXPECT_EQ(command.bytes[15], 0x6E);
    EXPECT_EQ(classify_command(command, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_KNOWN);
  }
  for (const size_t offset : {0, 8}) {
    for (const size_t omit : {0, 1}) {
      char text[format_hex_pretty_size(STATE_LENGTH)];
      auto bytes = frame(PKE_20C_AUTO);
      bytes[26] = 0;
      Command command;
      ASSERT_TRUE(parse_command_hex(
          StringRef(format_hex_pretty_to(text, bytes.data() + offset, 27 - offset - omit, ' ')), command));
      EXPECT_EQ(command.length, 27);
      EXPECT_EQ(Frame(command.bytes, command.bytes + 27), frame(PKE_20C_AUTO));
    }
  }
}

TEST(PanasonicProtocol, RejectsMalformedHexWithoutSending) {
  Fixture f(Model::RKR);
  for (const char *input :
       {"", "0220E00480B63", "02 20 E0 04 80 B6 GG", "0 2 20 E0 04 80 B6 32", "02,20,E0,04,80,B6,32",
        "12 20 E0 04 80 B6 32", "02 20 E0 04 00 B6 32", "02 20 E0 04 00 00 00 07 02 20 E0 04 80 B6 32",
        "0220E004000000060220E00400492880AF00000EE000008100001500"}) {
    EXPECT_FALSE(f.climate.send_command_hex(input)) << input;
  }
  EXPECT_TRUE(f.transmitter.frames.empty());
}

TEST(PanasonicProtocol, KnownShortActionsAndUnknownModel) {
  const std::pair<const char *, ShortAction> cases[] = {
      {"0220E00480B632", ShortAction::SHORT_ACTION_CANCEL_ALL},
      {"0220E004809C32", ShortAction::SHORT_ACTION_POWERFUL_QUIET},
      {"0220E004806133", ShortAction::SHORT_ACTION_NANOE_G},
      {"0220E0048068F2", ShortAction::SHORT_ACTION_ECONAVI_ON},
      {"0220E004805233", ShortAction::SHORT_ACTION_ECONAVI_OFF},
  };
  for (const auto &[input, action] : cases) {
    Command command;
    ASSERT_TRUE(parse_command_hex(StringRef(input), command));
    const auto received = decode_command(command_capture(command));
    EXPECT_EQ(classify_command(received, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_KNOWN);
    EXPECT_EQ(short_action(received, Model::RKR), action);
    EXPECT_EQ(classify_command(received, Model::DKE), CommandStatus::COMMAND_STATUS_VALID_UNKNOWN);
  }
}

TEST(PanasonicProtocol, ClassifiesUnknownFieldsAndBothBadChecksums) {
  Command command;
  ASSERT_TRUE(parse_command_hex(StringRef("0220E00480FE33"), command));
  EXPECT_EQ(classify_command(decode_command(command_capture(command)), Model::RKR),
            CommandStatus::COMMAND_STATUS_VALID_UNKNOWN);
  for (const size_t index : {7, 15}) {
    auto bad = command;
    bad.bytes[index] ^= 1;
    EXPECT_EQ(classify_command(decode_command(command_capture(bad)), Model::RKR),
              CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM);
  }
  FrameParams params;
  params.model = Model::RKR;
  encode_frame(params, command.bytes);
  command.length = 27;
  EXPECT_EQ(classify_command(command, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_KNOWN);
  command.bytes[25] |= 0x80;
  command.bytes[26] = calc_checksum(command.bytes, 27);
  EXPECT_EQ(classify_command(decode_command(command_capture(command)), Model::RKR),
            CommandStatus::COMMAND_STATUS_VALID_UNKNOWN);
}

TEST(PanasonicProtocol, SplitAndMalformedCapturesAreExplicit) {
  Command command;
  ASSERT_TRUE(parse_command_hex(StringRef("0220E00480B632"), command));
  RawTimings split;
  add_section(split, command.bytes + 8, 8);
  split.push_back(-5000);
  const auto received = decode_command(split);
  EXPECT_TRUE(received.reconstructed_prefix);
  EXPECT_EQ(classify_command(received, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_KNOWN);
  EXPECT_EQ(classify_command(decode_command(capture(PKE_20C_AUTO, Sections::FIRST, 5000)), Model::RKR),
            CommandStatus::COMMAND_STATUS_INCOMPLETE);
  auto truncated = command_capture(command);
  truncated.resize(truncated.size() - 10);
  const auto partial = decode_command(truncated);
  EXPECT_GT(partial.length, 8);
  EXPECT_EQ(classify_command(partial, Model::RKR), CommandStatus::COMMAND_STATUS_INCOMPLETE);
  auto extra = command_capture(command);
  extra.push_back(-5000);
  extra.push_back(432);
  EXPECT_EQ(classify_command(decode_command(extra), Model::RKR), CommandStatus::COMMAND_STATUS_INCOMPLETE);
  command.bytes[0] = 0x12;
  EXPECT_EQ(classify_command(decode_command(command_capture(command)), Model::RKR),
            CommandStatus::COMMAND_STATUS_UNRELATED);
}

TEST(PanasonicProtocol, ShortReceiveAndRawTransmitNeverChangeClimateState) {
  Fixture f(Model::RKR);
  ASSERT_TRUE(f.receive(RKR_FAN_TIMER_OFF_0020));
  const auto mode = f.climate.mode;
  const auto temperature = f.climate.target_temperature;
  const auto fan = f.climate.get_custom_fan_mode();
  const auto swing = f.climate.swing_mode;
  ASSERT_TRUE(f.climate.send_command_hex("0220E00480B632"));
  ASSERT_EQ(f.transmitter.frames.size(), 1);
  const auto sent = decode_command(f.transmitter.frames.back());
  EXPECT_EQ(sent.length, 16);
  EXPECT_EQ(sent.bytes[15], 0x6E);
  EXPECT_TRUE(f.receive(f.transmitter.frames.back()));
  auto bad = sent;
  bad.bytes[15] ^= 1;
  EXPECT_FALSE(f.receive(command_capture(bad)));
  EXPECT_EQ(f.climate.mode, mode);
  EXPECT_EQ(f.climate.target_temperature, temperature);
  EXPECT_EQ(f.climate.get_custom_fan_mode(), fan);
  EXPECT_EQ(f.climate.swing_mode, swing);
  ASSERT_TRUE(f.climate.send_command_hex("0220E00400492880AF00000EE00000810000"));
  EXPECT_EQ(decode_command(f.transmitter.frames.back()).bytes[26], 0x15);
  EXPECT_EQ(f.climate.mode, mode);
  EXPECT_EQ(f.climate.target_temperature, temperature);
}

// SET captures from A75C4762; these prove packing, not the effects of combined slot-2 writes.
static const uint8_t TIMER_CAPTURES[][STATE_LENGTH] = {
    {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    0x06, 0x02, 0x20, 0xE0, 0x04, 0,   0x3B,
     0x38, 0x80, 0x32, 0x0A, 0x68, 0x29, 0xD8, 0,    0,    0x80, 0xCC, 0x04, 0xEE},
    {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    0x06, 0x02, 0x20, 0xE0, 0x04, 0,   0x3B,
     0x38, 0x80, 0x32, 0x0A, 0xFC, 0x2B, 0xD8, 0,    0,    0x80, 0xCB, 0x44, 0xC3},
    {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    0x06, 0x02, 0x20, 0xE0, 0x04, 0,   0x3D,
     0x38, 0x80, 0x32, 0x0A, 0x68, 0x29, 0xD8, 0,    0,    0x80, 0xD5, 0x04, 0xF9},
    {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    0x06, 0x02, 0x20, 0xE0, 0x04, 0,   0x3D,
     0x38, 0x80, 0x32, 0x0A, 0x68, 0x29, 0x88, 0,    0,    0x80, 0xD5, 0x84, 0x29},
};

TEST(PanasonicTimers, ReproducesReferenceTimerFieldsExactly) {
  const uint16_t on[]{360, 1020, 360, 360};
  const uint16_t off[]{1410, 1410, 1410, 130};
  const uint16_t clock[]{1228, 1227, 1237, 1237};
  for (size_t i = 0; i < 4; i++) {
    auto fields = decode_rkr_timers(TIMER_CAPTURES[i]);
    EXPECT_EQ(fields.on_minutes, on[i]);
    EXPECT_EQ(fields.off_minutes, off[i]);
    EXPECT_EQ(fields.clock_minutes, clock[i]);
    EXPECT_EQ(fields.on_slot2, i == 1);
    EXPECT_EQ(fields.off_slot2, i == 3);
    EXPECT_EQ(fields.on_enabled, i < 2);
    EXPECT_EQ(fields.off_enabled, i >= 2);
    EXPECT_TRUE(fields.on_unknown_flag);
    EXPECT_TRUE(fields.off_unknown_flag);
    uint8_t encoded[STATE_LENGTH];
    memcpy(encoded, TIMER_CAPTURES[i], STATE_LENGTH);
    for (const size_t index : {13, 18, 19, 20, 23, 24, 25, 26})
      encoded[index] = index == 13 ? 0x31 : 0;
    ASSERT_TRUE(encode_rkr_timers(fields, encoded));
    EXPECT_EQ(frame(encoded), frame(TIMER_CAPTURES[i]));
  }
}

TEST(PanasonicTimers, EveryTenMinuteTimeAndEnableCombinationPreservesOperatingBytes) {
  for (uint16_t minutes = 0; minutes < 1440; minutes += 10) {
    for (uint8_t flags = 0; flags < 4; flags++) {
      uint8_t encoded[STATE_LENGTH];
      memcpy(encoded, TIMER_CAPTURES[0], STATE_LENGTH);
      const RkrTimerFields fields{.on_minutes = minutes,
                                  .off_minutes = static_cast<uint16_t>(1430 - minutes),
                                  .clock_minutes = 1439,
                                  .on_enabled = (flags & 1) != 0,
                                  .off_enabled = (flags & 2) != 0};
      ASSERT_TRUE(encode_rkr_timers(fields, encoded));
      EXPECT_EQ(encoded[13], 0x39 | ((flags & 1) << 1) | ((flags & 2) << 1));
      EXPECT_EQ(encoded[23], 0x80);
      EXPECT_EQ(encoded[25], 0xC5);
      EXPECT_EQ(encoded[24], 0x9F);
      EXPECT_EQ(encoded[26], sum_bytes(encoded + 8, 18));
      const auto decoded = decode_rkr_timers(encoded);
      EXPECT_EQ(decoded.on_minutes, minutes);
      EXPECT_EQ(decoded.off_minutes, 1430 - minutes);
      EXPECT_EQ(decoded.clock_minutes, 1439);
      for (size_t i = 0; i < 27; i++)
        if (i != 13 && i != 18 && i != 19 && i != 20 && i != 23 && i != 24 && i != 25 && i != 26)
          EXPECT_EQ(encoded[i], TIMER_CAPTURES[0][i]);
    }
  }
}

TEST(PanasonicTimers, Off2CancellationMatchesHardwareTestedContext) {
  Command programmed;
  ASSERT_TRUE(parse_command_hex(
      StringRef("02 20 E0 04 00 00 00 06 02 20 E0 04 00 6D 36 80 A1 06 68 89 D7 00 00 80 91 03"), programmed));
  Command cancelled;
  ASSERT_TRUE(parse_command_hex(
      StringRef("02 20 E0 04 00 00 00 06 02 20 E0 04 00 69 36 80 A1 06 68 29 D8 00 00 80 90 C3"), cancelled));
  uint8_t encoded[STATE_LENGTH];
  memcpy(encoded, programmed.bytes, STATE_LENGTH);
  encoded[23] = 0x89;  // Starting from the ordinary climate context must not leak into the update.
  const RkrTimerFields fields{.on_minutes = 360, .off_minutes = 1410, .clock_minutes = 912};
  ASSERT_TRUE(encode_rkr_timers(fields, encoded));
  EXPECT_EQ(frame(encoded), frame(cancelled.bytes));
  EXPECT_EQ(encoded[13], 0x69);
  EXPECT_EQ(encoded[23], 0x80);
  EXPECT_EQ(encoded[25], 0xC3);
  EXPECT_EQ(encoded[26], sum_bytes(encoded + 8, 18));
  auto ordinary_context = cancelled;
  ordinary_context.bytes[23] = 0x89;
  ordinary_context.bytes[26] = calc_checksum(ordinary_context.bytes, STATE_LENGTH);
  EXPECT_NE(encoded[26], ordinary_context.bytes[26]);
  // This compares encoding only; physical OFF1 preservation is recorded in PROTOCOL.md.
  for (size_t i = 0; i < 26; i++)
    if (i != 23)
      EXPECT_EQ(encoded[i], ordinary_context.bytes[i]);
}

TEST(PanasonicTimers, InvalidValuesDoNotModifyFrameAndFlagsAreIndependent) {
  for (uint16_t invalid : {1, 359, 1440, 65535}) {
    uint8_t encoded[STATE_LENGTH];
    memcpy(encoded, TIMER_CAPTURES[0], STATE_LENGTH);
    RkrTimerFields fields;
    fields.on_minutes = invalid;
    EXPECT_FALSE(encode_rkr_timers(fields, encoded));
    EXPECT_EQ(frame(encoded), frame(TIMER_CAPTURES[0]));
    fields.on_minutes = 360;
    fields.off_minutes = invalid;
    EXPECT_FALSE(encode_rkr_timers(fields, encoded));
  }
  for (uint8_t flags = 0; flags < 16; flags++) {
    uint8_t encoded[STATE_LENGTH];
    memcpy(encoded, TIMER_CAPTURES[0], STATE_LENGTH);
    RkrTimerFields fields{.clock_minutes = 0,
                          .on_slot2 = (flags & 1) != 0,
                          .off_slot2 = (flags & 2) != 0,
                          .on_unknown_flag = (flags & 4) != 0,
                          .off_unknown_flag = (flags & 8) != 0};
    ASSERT_TRUE(encode_rkr_timers(fields, encoded));
    EXPECT_EQ((encoded[25] >> 6) & 1, flags & 1);
    EXPECT_EQ((encoded[25] >> 7) & 1, (flags >> 1) & 1);
    EXPECT_EQ((encoded[19] >> 3) & 1, (flags >> 2) & 1);
    EXPECT_EQ((encoded[20] >> 7) & 1, (flags >> 3) & 1);
    fields.clock_minutes = 1440;
    EXPECT_FALSE(encode_rkr_timers(fields, encoded));
  }
}

class TimerClock : public time::RealTimeClock {
 public:
  time_t epoch{0};
  time_t timestamp_now() override { return this->epoch; }
  void update() override {}
};

TEST(PanasonicTimers, RestoreAndInvalidClockNeverTransmit) {
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  EXPECT_FALSE(timers.get_enabled(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_FALSE(timers.get_enabled(TimerSlot::TIMER_SLOT_OFF2));
  ASSERT_TRUE(timers.set_minutes(TimerSlot::TIMER_SLOT_ON2, 1020));
  timers.set_enabled(TimerSlot::TIMER_SLOT_ON2, true);
  timers.set_enabled(TimerSlot::TIMER_SLOT_OFF2, true);
  EXPECT_FALSE(timers.apply(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_TRUE(f.transmitter.frames.empty());
  RkrTimers restored(&f.climate, &clock);
  restored.setup();
  EXPECT_EQ(restored.get_minutes(TimerSlot::TIMER_SLOT_ON2), 1020);
  EXPECT_TRUE(restored.get_enabled(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_TRUE(restored.get_enabled(TimerSlot::TIMER_SLOT_OFF2));
  clock.epoch = 1767598198;
  EXPECT_TRUE(f.transmitter.frames.empty());
  ASSERT_TRUE(restored.apply(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_EQ(f.transmitter.frames.size(), 1);
}

TEST(PanasonicTimers, ChangesApplyOnlyTargetSlotAndClimateCommandsPreserveTimers) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(28);
  });
  const auto normal = f.last_state();
  EXPECT_EQ(normal[23], 0x89);
  for (uint8_t i = 0; i < 4; i++) {
    const auto slot = static_cast<TimerSlot>(i);
    const bool on = i < 2;
    for (const bool enabled : {true, false}) {
      const auto before = f.transmitter.frames.size();
      timers.set_enabled(slot, enabled);
      ASSERT_EQ(f.transmitter.frames.size(), before + 1);
      auto fields = decode_rkr_timers(f.last_state().data());
      EXPECT_EQ(on ? fields.on_enabled : fields.off_enabled, enabled);
      EXPECT_TRUE(on ? fields.off_enabled : fields.on_enabled);
      EXPECT_EQ(on ? fields.off_minutes : fields.on_minutes, TIMER_TIME_SPECIAL);
      EXPECT_EQ(fields.on_slot2, i == 1);
      EXPECT_EQ(fields.off_slot2, i == 3);
      EXPECT_TRUE(fields.on_unknown_flag);
      EXPECT_TRUE(fields.off_unknown_flag);
      ASSERT_TRUE(timers.set_minutes(slot, 1110));
      EXPECT_EQ(f.transmitter.frames.size(), before + 1);
      ASSERT_TRUE(timers.apply(slot));
      fields = decode_rkr_timers(f.last_state().data());
      EXPECT_EQ(on ? fields.on_minutes : fields.off_minutes, 1110);
      EXPECT_EQ(on ? fields.on_enabled : fields.off_enabled, enabled);
      ASSERT_TRUE(timers.apply(slot));
      const auto update = f.last_state();
      EXPECT_EQ(update[23], 0x80);
      EXPECT_EQ(update[26], calc_checksum(update.data(), STATE_LENGTH));
      for (size_t j = 0; j < 27; j++)
        if (j != 13 && j != 18 && j != 19 && j != 20 && j != 23 && j != 24 && j != 25 && j != 26)
          EXPECT_EQ(update[j], normal[j]);
    }
  }
  f.command([](auto &call) { call.set_fan_mode(climate::CLIMATE_FAN_HIGH); });
  EXPECT_EQ(f.last_state()[13] & 8, 0);
  EXPECT_EQ(f.last_state()[23], 0x89);
  f.climate.set_vane_position(2);
  EXPECT_EQ(f.last_state()[13] & 8, 0);
  EXPECT_EQ(f.last_state()[23], 0x89);
  f.climate.set_horizontal_vane_position(2);
  EXPECT_EQ(f.last_state()[13] & 8, 0);
  EXPECT_EQ(f.last_state()[23], 0x89);
  ASSERT_TRUE(f.receive(TIMER_CAPTURES[0]));
  for (uint8_t i = 0; i < 4; i++) {
    EXPECT_FALSE(timers.get_enabled(static_cast<TimerSlot>(i)));
    EXPECT_EQ(timers.get_minutes(static_cast<TimerSlot>(i)), 1110);
  }
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });
  EXPECT_EQ(f.last_state()[13] & 8, 0);
  EXPECT_EQ(f.last_state()[23], 0x89);
}

class PendingTimers : public RkrTimers {
 public:
  using RkrTimers::RkrTimers;
  using RkrTimers::process_pending_;
};

TEST(PanasonicTimers, ApplyAllSendsTwoPairedOperationsAndStopsOnInvalidClock) {
  Fixture f(Model::RKR);
  TimerClock clock;
  PendingTimers timers(&f.climate, &clock);
  timers.setup();
  EXPECT_FALSE(timers.apply_all());
  clock.epoch = 1767598198;
  ASSERT_TRUE(timers.apply_all());
  EXPECT_TRUE(f.transmitter.frames.empty());
  for (uint8_t i = 0; i < 2; i++) {
    timers.process_pending_();
    ASSERT_EQ(f.transmitter.frames.size(), i + 1);
    const auto fields = decode_rkr_timers(f.last_state().data());
    EXPECT_EQ(fields.on_slot2, i == 1);
    EXPECT_EQ(fields.off_slot2, i == 1);
    EXPECT_EQ(fields.on_minutes, 360);
    EXPECT_EQ(fields.off_minutes, 1410);
  }
  timers.process_pending_();
  EXPECT_EQ(f.transmitter.frames.size(), 2);
  ASSERT_TRUE(timers.apply_all());
  timers.process_pending_();
  clock.epoch = 0;
  timers.process_pending_();
  clock.epoch = 1767598198;
  timers.process_pending_();
  EXPECT_EQ(f.transmitter.frames.size(), 3);
}

TEST(PanasonicTimers, OnceSurvivesRebootAndUsesSubmittedTimeForCleanup) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  const auto slot = TimerSlot::TIMER_SLOT_OFF2;
  timers.set_minutes(slot, 0);
  timers.set_mode(slot, 2);
  timers.set_minutes(slot, 120);
  RkrTimers restored(&f.climate, &clock);
  restored.setup();
  EXPECT_EQ(f.transmitter.frames.size(), 1);
  clock.epoch = 0;
  restored.cleanup_();
  EXPECT_EQ(f.transmitter.frames.size(), 1);
  clock.epoch = 1767598198 + 90000;
  restored.cleanup_();
  restored.cleanup_();
  ASSERT_EQ(f.transmitter.frames.size(), 2);
  auto fields = decode_rkr_timers(f.last_state().data());
  EXPECT_FALSE(fields.off_enabled);
  EXPECT_EQ(fields.off_minutes, 0);
  EXPECT_EQ(restored.get_minutes(slot), 120);
  EXPECT_EQ(restored.get_mode(slot), 0);
}

TEST(PanasonicTimers, RemoteSetInvalidatesOnceWithOptionalImport) {
  for (bool accept : {false, true}) {
    Fixture f(Model::RKR);
    TimerClock clock;
    clock.epoch = 1767598198;
    RkrTimers timers(&f.climate, &clock);
    timers.setup();
    timers.set_accept_received_timers(accept);
    const auto slot = TimerSlot::TIMER_SLOT_ON2;
    timers.set_mode(slot, 2);
    ASSERT_TRUE(f.receive(TIMER_CAPTURES[1]));
    EXPECT_EQ(timers.get_mode(slot), accept ? 1 : 2);
    clock.epoch += 90000;
    timers.cleanup_();
    EXPECT_EQ(f.transmitter.frames.size(), 1);
    timers.cancel_all();
    auto cmd = decode_command(f.transmitter.frames.back());
    EXPECT_EQ(short_action(cmd, Model::RKR), ShortAction::SHORT_ACTION_CANCEL_ALL);
    EXPECT_EQ(timers.get_mode(slot), 0);
  }
}

TEST(PanasonicTimers, FeatureButtonsSendIdentifiedPayloadsAndRestoreCool) {
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  for (unsigned i = 0; i < 4; i++) {
    timers.feature(i);
    auto cmd = decode_command(f.transmitter.frames.back());
    const uint8_t expected[] = {0x52, 0x68, 0x9C, 0x61};
    EXPECT_EQ(cmd.length, 16);
    EXPECT_EQ(cmd.bytes[13], expected[i]);
    EXPECT_TRUE(f.receive(f.transmitter.frames.back()));
  }
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(30);
  });
  timers.feature(4);
  EXPECT_EQ(f.last_state()[25], 0x80);
  EXPECT_EQ(f.last_state()[14], 0x32);
  timers.feature(4);
  EXPECT_EQ(f.last_state()[25], 0);
  EXPECT_EQ(f.last_state()[14], 0x3C);
}

TEST(PanasonicTimers, ReprogrammingOnceSupersedesPreviousCleanup) {
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  clock.epoch = 1767598200;
  auto now = clock.now();
  auto slot = TimerSlot::TIMER_SLOT_OFF1;
  unsigned base = now.hour * 60 + now.minute;
  timers.set_minutes(slot, (base + 10) / 10 * 10 % 1440);
  timers.set_mode(slot, 2);
  timers.set_minutes(slot, (base + 90) / 10 * 10 % 1440);
  timers.apply(slot);
  clock.epoch += 3600;
  timers.cleanup_();
  EXPECT_EQ(f.transmitter.frames.size(), 2);
  timers.set_mode(slot, 1);
  clock.epoch += 86400;
  timers.cleanup_();
  EXPECT_EQ(f.transmitter.frames.size(), 3);
  EXPECT_EQ(timers.get_mode(slot), 1);
}

TEST(PanasonicTimers, MildDrySurvivesCoolClimateAndTimerWrites) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(30);
  });
  auto cmd = decode_command(f.transmitter.frames.back());
  cmd.bytes[15] = 0x7F;
  cmd.bytes[26] = calc_checksum(cmd.bytes, STATE_LENGTH);
  ASSERT_TRUE(f.receive(command_capture(cmd)));
  f.climate.set_vane_position(3);
  EXPECT_EQ(f.last_state()[15], 0x7F);
  timers.apply(TimerSlot::TIMER_SLOT_OFF2);
  EXPECT_EQ(f.last_state()[15], 0x7F);
  EXPECT_EQ(f.last_state()[25] & 0x80, 0x80);
  cmd.bytes[15] = 0x80;
  cmd.bytes[26] = calc_checksum(cmd.bytes, STATE_LENGTH);
  ASSERT_TRUE(f.receive(command_capture(cmd)));
  f.climate.set_vane_position(2);
  EXPECT_EQ(f.last_state()[15], 0x80);
}
TEST(PanasonicTimers, ReceivedOperatingIautoIsDistinctFromOff2Selector) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  f.command([](auto &call) {
    call.set_mode(climate::CLIMATE_MODE_COOL);
    call.set_target_temperature(30);
  });
  auto cmd = decode_command(f.transmitter.frames.back());
  cmd.bytes[13] = 0x09;
  cmd.bytes[14] = 0x32;
  cmd.bytes[25] = 0x80;
  cmd.bytes[26] = calc_checksum(cmd.bytes, STATE_LENGTH);
  ASSERT_EQ(classify_command(cmd, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_KNOWN);
  ASSERT_TRUE(f.receive(command_capture(cmd)));
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_HEAT_COOL);
  timers.apply(TimerSlot::TIMER_SLOT_OFF1);
  EXPECT_EQ(f.last_state()[25] & 0x80, 0);
  f.climate.set_vane_position(3);
  EXPECT_EQ(f.last_state()[25] & 0x80, 0x80);
  timers.feature(4);
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_COOL);
  EXPECT_EQ(f.climate.target_temperature, 30);
  f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });
  timers.feature(4);
  EXPECT_EQ(f.last_state()[13] & 1, 1);
  EXPECT_EQ(f.last_state()[25] & 0x80, 0x80);
}
TEST(PanasonicTimers, ExternalShortCommandsPublishWithoutTxAndOwnEchoIsIgnored) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  timers.set_accept_received_timers(true);
  EconaviSwitch eco(&timers);
  eco.setup();
  EXPECT_TRUE(eco.assumed_state());
  eco.turn_on();
  eco.turn_on();
  ASSERT_EQ(f.transmitter.frames.size(), 2);
  eco.publish_state(false);
  ASSERT_TRUE(f.receive(f.transmitter.frames.back()));
  EXPECT_FALSE(eco.state);
  ASSERT_TRUE(f.receive(f.transmitter.frames.back()));
  EXPECT_TRUE(eco.state);
  eco.turn_off();
  EXPECT_FALSE(eco.state);
  timers.set_mode(TimerSlot::TIMER_SLOT_ON1, 2);
  Command cancel;
  ASSERT_TRUE(parse_command_hex(StringRef("0220E00480B632"), cancel));
  ASSERT_TRUE(f.receive(command_capture(cancel)));
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON1), 0);
  clock.epoch += 90000;
  timers.cleanup_();
  EXPECT_EQ(f.transmitter.frames.size(), 4);
}

TEST(PanasonicTimers, ExternalIndividualCancellationRetainsOppositeSlot) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  timers.set_accept_received_timers(true);
  timers.set_mode(TimerSlot::TIMER_SLOT_ON1, 1);
  timers.set_mode(TimerSlot::TIMER_SLOT_OFF2, 2);
  auto cmd = decode_command(f.transmitter.frames.back());
  auto fields = decode_rkr_timers(cmd.bytes);
  fields.off_enabled = false;
  fields.off_minutes = TIMER_TIME_SPECIAL;
  ASSERT_TRUE(encode_rkr_timers(fields, cmd.bytes));
  ASSERT_TRUE(f.receive(command_capture(cmd)));
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON1), 1);
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_OFF2), 0);
  EXPECT_EQ(timers.get_minutes(TimerSlot::TIMER_SLOT_OFF2), 1410);
  clock.epoch += 90000;
  timers.cleanup_();
  EXPECT_EQ(f.transmitter.frames.size(), 2);
}
TEST(PanasonicTimers, IndependentOperationsCoverEveryTimeSlotAndEnableCombination) {
  for (uint8_t slot = 0; slot < 4; slot++) {
    for (uint16_t minutes = 0; minutes < 1440; minutes += 10) {
      for (const bool enabled : {false, true}) {
        RkrTimerFields fields;
        ASSERT_TRUE(make_rkr_timer_operation(static_cast<TimerSlot>(slot), minutes, enabled, 1439, fields));
        uint8_t bytes[STATE_LENGTH];
        memcpy(bytes, TIMER_CAPTURES[0], STATE_LENGTH);
        ASSERT_TRUE(encode_rkr_timers(fields, bytes));
        const auto decoded = decode_rkr_timers(bytes);
        EXPECT_EQ(decoded.on_minutes, slot < 2 ? minutes : TIMER_TIME_SPECIAL);
        EXPECT_EQ(decoded.off_minutes, slot < 2 ? TIMER_TIME_SPECIAL : minutes);
        EXPECT_EQ(decoded.on_enabled, slot < 2 ? enabled : true);
        EXPECT_EQ(decoded.off_enabled, slot < 2 ? true : enabled);
        EXPECT_EQ(decoded.on_slot2, slot == 1);
        EXPECT_EQ(decoded.off_slot2, slot == 3);
        EXPECT_EQ(bytes[23], 0x80);
        EXPECT_EQ(bytes[25], 5 | (slot == 1 ? 0x40 : 0) | (slot == 3 ? 0x80 : 0));
        EXPECT_EQ(bytes[19] & 8, 8);
        EXPECT_EQ(bytes[20] & 0x80, 0x80);
        EXPECT_EQ(bytes[26], calc_checksum(bytes, STATE_LENGTH));
      }
    }
  }
  RkrTimerFields fields;
  EXPECT_FALSE(make_rkr_timer_operation(static_cast<TimerSlot>(4), 360, true, 0, fields));
  EXPECT_FALSE(make_rkr_timer_operation(TimerSlot::TIMER_SLOT_ON1, TIMER_TIME_SPECIAL, false, 0, fields));
  EXPECT_FALSE(make_rkr_timer_operation(TimerSlot::TIMER_SLOT_ON1, 361, true, 0, fields));
  EXPECT_FALSE(make_rkr_timer_operation(TimerSlot::TIMER_SLOT_ON1, 360, true, 1440, fields));
}

TEST(PanasonicTimers, OppositePreservationIgnoresAllRememberedEnableCombinations) {
  for (uint8_t enables = 0; enables < 16; enables++) {
    Fixture f(Model::RKR);
    TimerClock clock;
    RkrTimers timers(&f.climate, &clock);
    timers.setup();
    for (uint8_t i = 0; i < 4; i++) {
      timers.set_minutes(static_cast<TimerSlot>(i), 60 * (i + 1));
      timers.set_enabled(static_cast<TimerSlot>(i), enables & (1U << i));
    }
    EXPECT_TRUE(f.transmitter.frames.empty());
    clock.epoch = 1767598198;
    for (uint8_t target = 0; target < 4; target++) {
      ASSERT_TRUE(timers.apply(static_cast<TimerSlot>(target)));
      const auto fields = decode_rkr_timers(f.last_state().data());
      EXPECT_EQ(target < 2 ? fields.on_enabled : fields.off_enabled, (enables & (1U << target)) != 0);
      EXPECT_TRUE(target < 2 ? fields.off_enabled : fields.on_enabled);
      EXPECT_EQ(target < 2 ? fields.off_minutes : fields.on_minutes, TIMER_TIME_SPECIAL);
      for (uint8_t i = 0; i < 4; i++) {
        EXPECT_EQ(timers.get_enabled(static_cast<TimerSlot>(i)), (enables & (1U << i)) != 0);
        EXPECT_EQ(timers.get_minutes(static_cast<TimerSlot>(i)), 60 * (i + 1));
      }
    }
  }
}

TEST(PanasonicTimers, MigratesLegacySlotTwoIntentWithoutTransmission) {
  Fixture f(Model::RKR);
  TimerClock clock;
  struct LegacySettings {
    uint16_t on_minutes;
    uint16_t off_minutes;
    uint8_t on_enabled;
    uint8_t off_enabled;
  };
  auto preference = f.climate.make_entity_preference<LegacySettings>(0x524B5201);
  const LegacySettings old{1020, 130, 1, 1};
  ASSERT_TRUE(preference.save(&old));
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  EXPECT_EQ(timers.get_minutes(TimerSlot::TIMER_SLOT_ON2), 1020);
  EXPECT_EQ(timers.get_minutes(TimerSlot::TIMER_SLOT_OFF2), 130);
  EXPECT_TRUE(timers.get_enabled(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_TRUE(timers.get_enabled(TimerSlot::TIMER_SLOT_OFF2));
  EXPECT_FALSE(timers.get_enabled(TimerSlot::TIMER_SLOT_ON1));
  EXPECT_FALSE(timers.get_enabled(TimerSlot::TIMER_SLOT_OFF1));
  timers.set_minutes(TimerSlot::TIMER_SLOT_ON1, 420);
  timers.set_enabled(TimerSlot::TIMER_SLOT_OFF1, true);
  RkrTimers restored(&f.climate, &clock);
  restored.setup();
  EXPECT_EQ(restored.get_minutes(TimerSlot::TIMER_SLOT_ON1), 420);
  EXPECT_TRUE(restored.get_enabled(TimerSlot::TIMER_SLOT_OFF1));
  EXPECT_EQ(restored.get_minutes(TimerSlot::TIMER_SLOT_ON2), 1020);
  EXPECT_TRUE(f.transmitter.frames.empty());
}

TEST(PanasonicTimers, EntitiesRestoreSilentlyAndRejectUnsupportedResolution) {
  Fixture f(Model::RKR);
  TimerClock clock;
  clock.epoch = 1767598198;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  RkrTimerTime input(&timers, TimerSlot::TIMER_SLOT_ON2);
  RkrTimerSwitch enabled(&timers, TimerSlot::TIMER_SLOT_ON2);
  input.setup();
  enabled.setup();
  EXPECT_TRUE(f.transmitter.frames.empty());
  EXPECT_TRUE(enabled.assumed_state());
  input.make_call().set_time(17, 5, 0).perform();
  input.make_call().set_time(17, 0, 1).perform();
  EXPECT_TRUE(f.transmitter.frames.empty());
  EXPECT_EQ(input.hour, 6);
  input.make_call().set_time(17, 0, 0).perform();
  EXPECT_EQ(input.hour, 17);
  EXPECT_TRUE(f.transmitter.frames.empty());
  enabled.turn_on();
  EXPECT_TRUE(enabled.state);
  EXPECT_TRUE(decode_rkr_timers(f.last_state().data()).on_enabled);
  enabled.turn_off();
  EXPECT_FALSE(decode_rkr_timers(f.last_state().data()).on_enabled);
}

TEST(PanasonicTimers, RefusesOtherModelsAndNonIndependentOperations) {
  RkrTimerFields fields;
  ASSERT_TRUE(make_rkr_timer_operation(TimerSlot::TIMER_SLOT_ON1, 360, true, 735, fields));
  for (const auto model : {Model::DKE, Model::JKE, Model::LKE, Model::NKE, Model::CKP, Model::PKE}) {
    Fixture f(model);
    EXPECT_FALSE(f.climate.send_timer_state(fields));
    EXPECT_TRUE(f.transmitter.frames.empty());
  }
  Fixture f(Model::RKR);
  EXPECT_TRUE(f.climate.send_timer_state(fields));
  EXPECT_TRUE(f.climate.send_timer_state(RkrTimerFields{}));
  fields.off_enabled = false;
  EXPECT_FALSE(f.climate.send_timer_state(fields));
  fields.off_enabled = true;
  fields.on_unknown_flag = false;
  EXPECT_FALSE(f.climate.send_timer_state(fields));
  EXPECT_EQ(f.transmitter.frames.size(), 2);
}

TEST(PanasonicTimers, AtomicOperatingApiPublishesOneTimerNeutralFrame) {
  Fixture f(Model::RKR);
  ASSERT_TRUE(f.climate.api_set(StringRef("ON"), StringRef("COOL"), 20, StringRef("MEDIUM"), StringRef("2"),
                                StringRef("Right"), {}, {}, {}, {}));
  ASSERT_EQ(f.transmitter.frames.size(), 1);
  auto bytes = f.last_state();
  EXPECT_EQ(bytes[13], 0x31);
  EXPECT_EQ(bytes[14], 40);
  EXPECT_EQ(bytes[16], 0x52);
  EXPECT_EQ(bytes[17], VANE_H_FULL_RIGHT);
  EXPECT_EQ(bytes[23], 0x89);
  EXPECT_EQ(bytes[26], calc_checksum(bytes.data(), STATE_LENGTH));
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_COOL);
  EXPECT_EQ(f.climate.target_temperature, 20);
  EXPECT_FALSE(f.climate.api_set({}, StringRef("HEAT"), 22, StringRef("AUTO"), {}, {}, {}, {}, StringRef("ON"), {}));
  EXPECT_EQ(f.transmitter.frames.size(), 1);
  ASSERT_TRUE(f.climate.api_set({}, {}, -1, {}, {}, {}, {}, {}, {}, {}));
  EXPECT_EQ(f.last_state(), bytes);
  ASSERT_TRUE(f.climate.api_set(StringRef("OFF"), {}, -1, {}, {}, {}, {}, {}, {}, {}));
  EXPECT_EQ(f.last_state()[13], 0x30);
  EXPECT_EQ(f.climate.mode, climate::CLIMATE_MODE_OFF);
  ASSERT_TRUE(f.climate.api_set(StringRef("ON"), {}, -1, {}, {}, {}, {}, {}, {}, {}));
  EXPECT_EQ(f.last_state(), bytes);
  EXPECT_EQ(f.transmitter.frames.size(), 4);
  ASSERT_TRUE(f.climate.api_set(StringRef("OFF"), {}, -1, {}, {}, {}, {}, {}, {}, {}));
  ASSERT_TRUE(f.climate.api_set({}, StringRef("HEAT"), 22, StringRef("AUTO"), {}, {}, {}, {}, {}, {}));
  EXPECT_EQ(f.last_state()[13], 0x40);
  ASSERT_TRUE(f.climate.api_set(StringRef("ON"), {}, -1, {}, {}, {}, {}, {}, {}, {}));
  EXPECT_EQ(f.last_state()[13], 0x41);
}
TEST(PanasonicTimers, AtomicTimerApiValidatesThenCommitsOnlyOneSlot) {
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  for (unsigned i = 0; i < 4; i++) {
    auto slot = static_cast<TimerSlot>(i);
    StringRef type(i == 0 ? "ON1" : i == 1 ? "ON2" : i == 2 ? "OFF1" : "OFF2");
    clock.epoch = 0;
    EXPECT_FALSE(timers.api_set(type, StringRef("00:00"), -1, StringRef("once")));
    clock.epoch = 1767598198;
    for (auto bad : {"24:00", "6:30", "06:60", "06:31", "06-30"})
      EXPECT_FALSE(timers.api_set(type, StringRef(bad), -1, StringRef("once")));
    EXPECT_FALSE(timers.api_set(type, StringRef("06:30"), 30, StringRef("once")));
    auto count = f.transmitter.frames.size();
    ASSERT_TRUE(timers.api_set(type, StringRef("00:00"), -1, StringRef("once")));
    EXPECT_EQ(f.transmitter.frames.size(), count + 1);
    EXPECT_EQ(timers.get_mode(slot), 2);
    EXPECT_EQ(timers.get_minutes(slot), 0);
    auto frame = f.last_state();
    auto fields = decode_rkr_timers(frame.data());
    EXPECT_EQ(frame[26], calc_checksum(frame.data(), STATE_LENGTH));
    EXPECT_EQ(frame[23], 0x80);
    EXPECT_EQ(fields.on_slot2, i == 1);
    EXPECT_EQ(fields.off_slot2, i == 3);
    EXPECT_EQ(i < 2 ? fields.off_minutes : fields.on_minutes, TIMER_TIME_SPECIAL);
    ASSERT_TRUE(timers.api_set(type, {}, -1, StringRef("daily")));
    clock.epoch += 90000;
    timers.cleanup_();
    EXPECT_EQ(f.transmitter.frames.size(), count + 2);
    ASSERT_TRUE(timers.api_set(type, {}, -1, StringRef("disabled")));
    EXPECT_EQ(timers.get_minutes(slot), 0);
    EXPECT_EQ(f.transmitter.frames.size(), count + 3);
  }
}
TEST(PanasonicTimers, UsesLocalClockIncludingSydneyDaylightSaving) {
  const auto previous = time::get_global_tz();
  time::ParsedTimezone sydney{};
  sydney.std_offset_seconds = -36000;
  sydney.dst_offset_seconds = -39600;
  sydney.dst_start = {.time_seconds = 7200,
                      .day = 0,
                      .type = time::DSTRuleType::MONTH_WEEK_DAY,
                      .month = 10,
                      .week = 1,
                      .day_of_week = 0};
  sydney.dst_end = {.time_seconds = 10800,
                    .day = 0,
                    .type = time::DSTRuleType::MONTH_WEEK_DAY,
                    .month = 4,
                    .week = 1,
                    .day_of_week = 0};
  time::set_global_tz(sydney);
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  // 2026-01-05 07:29:58 UTC -> 18:29 AEDT; clock minutes are not rounded to timer resolution.
  clock.epoch = 1767598198;
  EXPECT_TRUE(timers.apply(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_EQ(decode_rkr_timers(f.last_state().data()).clock_minutes, 1109);
  // 2026-07-05 07:29:58 UTC -> 17:29 AEST.
  clock.epoch = 1783236598;
  EXPECT_TRUE(timers.apply(TimerSlot::TIMER_SLOT_ON2));
  EXPECT_EQ(decode_rkr_timers(f.last_state().data()).clock_minutes, 1049);
  // UTC 2026-10-03T15:59 -> next Sydney wall-clock occurrence plus 30 minutes.
  clock.epoch = 1791043140;
  timers.set_minutes(TimerSlot::TIMER_SLOT_ON2, 130);
  timers.set_mode(TimerSlot::TIMER_SLOT_ON2, 2);
  clock.epoch = 1791128399;
  timers.cleanup_();
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON2), 2);
  clock.epoch++;
  timers.cleanup_();
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON2), 0);
  // UTC 2026-04-04T15:50 -> next Sydney wall-clock occurrence plus 30 minutes.
  clock.epoch = 1775317800;
  timers.set_minutes(TimerSlot::TIMER_SLOT_ON2, 130);
  timers.set_mode(TimerSlot::TIMER_SLOT_ON2, 2);
  clock.epoch = 1775320799;
  timers.cleanup_();
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON2), 2);
  clock.epoch++;
  timers.cleanup_();
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON2), 0);
  // UTC 2026-11-04T12:59 -> next Sydney wall-clock occurrence plus 30 minutes.
  clock.epoch = 1793797140;
  timers.set_minutes(TimerSlot::TIMER_SLOT_ON2, 0);
  timers.set_mode(TimerSlot::TIMER_SLOT_ON2, 2);
  clock.epoch = 1793798999;
  timers.cleanup_();
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON2), 2);
  clock.epoch++;
  timers.cleanup_();
  EXPECT_EQ(timers.get_mode(TimerSlot::TIMER_SLOT_ON2), 0);
  clock.epoch = 1791043020;
  ASSERT_TRUE(timers.api_set(StringRef("OFF1"), StringRef("unchanged"), 10, StringRef("once")));
  EXPECT_EQ(decode_rkr_timers(f.last_state().data()).off_minutes, 190);
  clock.epoch = 1775318220;
  ASSERT_TRUE(timers.api_set(StringRef("OFF1"), StringRef("unchanged"), 10, StringRef("once")));
  EXPECT_EQ(decode_rkr_timers(f.last_state().data()).off_minutes, 130);
  clock.epoch = 1793797020;
  ASSERT_TRUE(timers.api_set(StringRef("OFF1"), StringRef("unchanged"), 10, StringRef("once")));
  EXPECT_EQ(decode_rkr_timers(f.last_state().data()).off_minutes, 10);
  clock.epoch = 1793762820;
  ASSERT_TRUE(timers.api_set(StringRef("OFF1"), StringRef("unchanged"), 60, StringRef("once")));
  EXPECT_EQ(decode_rkr_timers(f.last_state().data()).off_minutes, 930);
  time::set_global_tz(previous);
}

static const uint8_t RKR_OFF_ON1_RETAINED[STATE_LENGTH] = {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    6,    0x02,
                                                           0x20, 0xE0, 0x04, 0,    0x6A, 0x36, 0x80, 0xAF, 0x0D,
                                                           0,    0x0E, 0xE0, 0,    0,    0x89, 0,    0,    0x59};
static const uint8_t RKR_OFF_ON1_CANCELLED[STATE_LENGTH] = {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    6,    0x02,
                                                            0x20, 0xE0, 0x04, 0,    0x68, 0x36, 0x80, 0xAF, 0x0D,
                                                            0,    0x0E, 0xE0, 0,    0,    0x89, 0,    0,    0x57};
static const uint8_t RKR_ON1_SET[STATE_LENGTH] = {0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    6,    0x02,
                                                  0x20, 0xE0, 0x04, 0,    0x6A, 0x36, 0x80, 0xAF, 0x0D,
                                                  0x68, 0x89, 0xD7, 0,    0,    0x80, 0xDF, 2,    0x0B};

TEST(PanasonicProtocol, OrdinaryRkrEnableFlagsAreKnownWithoutMaskingUnknownFields) {
  for (const auto *bytes : {RKR_OFF_ON1_RETAINED, RKR_OFF_ON1_CANCELLED}) {
    auto command = decode_command(capture(bytes));
    EXPECT_EQ(classify_command(command, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_KNOWN);
    EXPECT_TRUE(has_rkr_ordinary_timer_fields(command));
    const auto timers = decode_rkr_timers(command.bytes);
    EXPECT_EQ(timers.on_minutes, TIMER_TIME_SPECIAL);
    EXPECT_EQ(timers.off_minutes, TIMER_TIME_SPECIAL);
    EXPECT_EQ(timers.clock_minutes, 0);
    EXPECT_EQ(command.bytes[26], calc_checksum(command.bytes, 27));
    for (const size_t index : {13, 15, 21, 22, 23, 25}) {
      auto unknown = command;
      unknown.bytes[index] ^= 0x80;
      unknown.bytes[26] = calc_checksum(unknown.bytes, 27);
      EXPECT_EQ(classify_command(unknown, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_UNKNOWN) << index;
    }
    command.bytes[26] ^= 1;
    EXPECT_EQ(classify_command(command, Model::RKR), CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM);
  }
  EXPECT_EQ(RKR_OFF_ON1_RETAINED[13] ^ RKR_OFF_ON1_CANCELLED[13], 2);
  for (size_t i = 0; i < 26; i++)
    if (i != 13)
      EXPECT_EQ(RKR_OFF_ON1_RETAINED[i], RKR_OFF_ON1_CANCELLED[i]);
}

TEST(PanasonicProtocol, GenuineOn1SetCaptureAndIntentionallyCorruptedClock) {
  auto set = decode_command(capture(RKR_ON1_SET));
  EXPECT_EQ(set.bytes[26], 0x0B);
  EXPECT_EQ(calc_checksum(set.bytes, 27), 0x0B);
  EXPECT_FALSE(has_rkr_ordinary_timer_fields(set));
  EXPECT_EQ(classify_command(set, Model::RKR), CommandStatus::COMMAND_STATUS_VALID_UNKNOWN);
  auto timers = decode_rkr_timers(set.bytes);
  EXPECT_EQ(timers.on_minutes, 360);
  EXPECT_EQ(timers.off_minutes, 1400);
  // Captured at 12:15:20; the clock field contains minutes, not seconds.
  EXPECT_EQ(timers.clock_minutes, 735);
  EXPECT_TRUE(timers.on_enabled);
  EXPECT_FALSE(timers.off_enabled);
  Fixture f(Model::RKR);
  EXPECT_TRUE(f.receive(capture(RKR_ON1_SET)));

  // Intentionally corrupted negative fixture, originating from a handover transcription error.
  auto corrupted = set;
  corrupted.bytes[24] = 0xCE;
  EXPECT_EQ(corrupted.bytes[26], 0x0B);
  EXPECT_EQ(calc_checksum(corrupted.bytes, 27), 0xFA);
  EXPECT_EQ(classify_command(corrupted, Model::RKR), CommandStatus::COMMAND_STATUS_INVALID_CHECKSUM);
  EXPECT_FALSE(f.receive(capture(corrupted.bytes)));
}

TEST(PanasonicTimers, OrdinaryRemoteEnableBitsNeverChangeHaIntentOrPreservationPolicy) {
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  timers.setup();
  for (const auto *bytes : {RKR_OFF_ON1_RETAINED, RKR_OFF_ON1_CANCELLED, RKR_ON1_SET}) {
    ASSERT_TRUE(f.receive(capture(bytes)));
    EXPECT_FALSE(timers.get_enabled(TimerSlot::TIMER_SLOT_ON2));
    EXPECT_FALSE(timers.get_enabled(TimerSlot::TIMER_SLOT_OFF2));
    f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_COOL); });
    EXPECT_EQ(f.last_state()[13] & 0x0E, 0);
    f.climate.set_vane_position(2);
    EXPECT_EQ(f.last_state()[13] & 8, 0);
    f.command([](auto &call) { call.set_mode(climate::CLIMATE_MODE_OFF); });
    EXPECT_EQ(f.last_state()[13] & 8, 0);
  }
}

}  // namespace esphome::panasonic_ir::testing
