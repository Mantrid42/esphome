#include "esphome/components/panasonic_ir/timer_datetime.h"
#include "esphome/components/panasonic_ir/panasonic_rkr_timer_frame.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "esphome/components/panasonic_ir/panasonic_ir.h"
#include "esphome/components/remote_base/remote_base.h"
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

TEST(PanasonicClimate, IautoButtonAlwaysEnables) {
  Fixture f(Model::RKR);
  for (int press = 0; press < 2; ++press) {
    f.climate.enable_iauto();
    auto bytes = f.last_state();
    EXPECT_EQ(bytes[13], 1);
    EXPECT_EQ(bytes[14], 50);
    EXPECT_EQ(bytes[25], 0x80);
    EXPECT_EQ(f.climate.fan_mode.value(), climate::CLIMATE_FAN_AUTO);
  }
}

class TimerClock : public time::RealTimeClock {
 public:
  time_t timestamp_now() override { return 1767598198; }
  void update() override {}
};

TEST(PanasonicTimers, NormalisesTimeAndImportsRemoteSetWithoutEcho) {
  Fixture f(Model::RKR);
  TimerClock clock;
  RkrTimers timers(&f.climate, &clock);
  f.climate.set_timer_controller(&timers);
  timers.setup();
  RkrTimerTime entity(&timers, TimerSlot::TIMER_SLOT_ON1);
  entity.setup();
  EXPECT_TRUE(f.transmitter.frames.empty());
  entity.make_call().set_time(1, 29, 59).perform();
  EXPECT_EQ(entity.hour, 1);
  EXPECT_EQ(entity.minute, 20);
  EXPECT_EQ(entity.second, 0);
  auto sent = f.last_state();
  EXPECT_EQ(decode_rkr_timers(sent.data()).on_minutes, 80);
  const uint8_t remote[]{0x02, 0x20, 0xE0, 0x04, 0,    0,    0,    6, 0x02, 0x20, 0xE0, 0x04, 0,   0x6A,
                         0x36, 0x80, 0xAF, 0x0D, 0x68, 0x89, 0xD7, 0, 0,    0x80, 0xDF, 2,    0x0B};
  ASSERT_TRUE(f.receive(remote));
  EXPECT_EQ(entity.hour, 6);
  EXPECT_TRUE(timers.get_enabled(TimerSlot::TIMER_SLOT_ON1));
  EXPECT_EQ(f.transmitter.frames.size(), 1u);
}

}  // namespace esphome::panasonic_ir::testing
