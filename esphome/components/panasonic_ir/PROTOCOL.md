# Panasonic short commands and timer research

Byte offsets below are zero-based across the complete transmission. Protocol structure and action compatibility are separate questions.

## Published observations

[Karakum22's 2013 CS-XE9DKE investigation](https://habr.com/ru/articles/189142/) documents two eight-byte sections for Quiet, Ionizer and Oxygen actions. Its prose has `E0 40`; its code and bit diagram give `E0 04`.

The first section is `02 20 E0 04 00 00 00 06`. The second is `02 20 E0 04 80 XX YY CC`, with `CC` the sum of its preceding seven bytes modulo 256.

| DKE action | XX YY CC |
|---|---|
| Quiet | 81 33 3A |
| Ionizer | 48 33 01 |
| Oxygen | 50 33 09 |

The article distinguishes ON/OFF enable bits (B13 bits 1/2) from update flags X/Y (B19 bit 3/B20 bit 7). It says SET/CANCEL asserts X/Y; ordinary commands can carry enabled status without applying it. Timer values use minutes since midnight; `0x600` denotes unset/unknown. Its B13 bit 3 is shown as constant 1, not an update flag. These are that author's model-specific observations, not verified RKR semantics.

[panasonic-ac-models' generator](https://github.com/selvakk2k/panasonic-ac-models/blob/main/panasonic_ac_models/ir.py), inspected separately from its [1.2.0 documentation](https://pypi.org/project/panasonic-ac-models/1.2.0/), constructs the same short-frame layout. The project targets Indian-market models.

| Library action | XX YY CC |
|---|---|
| Powerful/Turbo | 86 35 41 |
| Display toggle | 9E 32 56 |
| Self-clean | CB F2 43 |
| 80% capacity | 04 AA 34 |

The generator also lists other capacity presets, including aliases sharing identifiers. These identifiers are comparisons for future captures, not RKR commands to expose or transmit. Its decoder's default feature and power values are not evidence of physical state. No third-party implementation was copied.

[ESPHome's existing source comment](https://api-docs-dev.esphome.io/panasonic__ac__frame_8h_source) already acknowledges short Quiet/Powerful and Nanoe-G sections, ignored by the climate-state decoder. [IRremoteESP8266's timer investigation](https://github.com/crankyoldgit/IRremoteESP8266/issues/544) concerns CS-ME14CKPG/A75C2311 and full 27-byte frames; its timer/clock findings must retain that model provenance.

## Original RKR findings

The user's A75C4762-family remote produced:

```
02 20 E0 04 00 00 00 06
02 20 E0 04 80 B6 32 6E
```

The checksum is valid. The user physically verified that ALL CANCEL clears timers without changing fan speed in the tested state. Effects on other features and operating modes have not been established. No independent publication of this exact identifier was found in the reviewed sources.

Clearing B13 bit 3 in our tested RKR full-state payload preserved timers through fan and power changes; setting it in the original payload cleared them. `FrameParams::apply_timers` is a provisional transmission policy name for that observation, not proof of universal bit semantics. The current template also sets B19 bit 3 and B20 bit 7, so the interaction with those candidate update flags needs controlled investigation. No timer values, enable flags, slot selectors or clock are exposed by this policy.

`encode_short_command` builds prefixes and checksum from three payload bytes. `encode_waveform` handles 16- and 27-byte frames with the same LSB-first transport. Neither helper itself transmits. CANCEL ALL's future button should use the short path without changing climate state, after native-waveform hardware testing.

## Capture batches

Before the first batch, the user observed that MILD DRY transmits only in COOL, and ECONAVI does not transmit in FAN or iAUTO-X. The remote was switched to COOL for the batch. Earlier exploratory presses and that mode change are pre-batch data, not members of groups A–E. Keep iAUTO-X last to avoid introducing its mode restriction before ECONAVI.

First batch: A POWERFUL/QUIET ×5; B NANOE-G ×5; C ECONAVI ×5; D MILD DRY ×5; E iAUTO-X ×5. Allow about 3 seconds between presses and 10 seconds between groups. Use short presses, not holds. Keep the indoor receiver shielded and aim at IR3. Record the remote's initial mode/power and visible feature indicators. Do not reset between groups: transitions and interactions are evidence. A feature may be unavailable in the current mode; record no response rather than add unplanned presses. iAUTO-X is last because it may change the remote's mode; MILD DRY may depend on cooling. This does not obstruct waveform capture, but limits conclusions about unavailable features.

Retain each original API log message with receipt timestamp and bytes, plus a readable timestamped log, outside the repository. Do not rely on the dashboard's finite display buffer. Decode Pronto after reassembling continuation lines; check advertised word counts, headers, footer marks, byte alignment, prefixes and checksums. Use 25% pulse tolerance, matching the receiver, and compare decoded bytes rather than Pronto strings. Preserve failed captures, partial sections and unusual formats without repairing them silently. Report complete frames separately from valid split sections; associate multiple sections or frames with a button press only when timing supports it.

For every group, report expected presses, observed events, valid complete captures, partial/invalid captures, unique payloads and changed byte offsets. Compare by decoded action; cycling commands are not repetitions. A checksum failure is a suspected capture error, not a new command. A valid changed payload is a transition candidate, with feature state inferred only when supported by remote indicators or established semantics. Repeat only affected groups if evidence is incomplete.

Later batches, separately agreed: individual timer CANCEL; timer SET/enable with one time or slot varied; clock-only changes; operating-state captures around timer actions. Examine full-frame enable bits and candidate update flags alongside short commands. Independent cancellation, independent programming and independent clock updates remain separate hypotheses. Hardware behavior tests follow receiver-only mapping and require discussion first.

## First batch observations (2026-11-02)

The saved API capture contains 22 complete, checksum-valid transmissions: 16 short frames and six full-state frames. No malformed Pronto word counts, timing failures, partial sections or checksum failures were found with 25% tolerance. Four timing-separated bursts contain 5, 6, 5 and 6 frames; attribution of the middle bursts to the planned groups awaits confirmation. Missing planned presses must not be classified as capture corruption without evidence. Original Pronto strings vary while decoded bytes repeat.

The first five frames consistently carry `80 9C 32 54` in the short section's final four bytes, associating this action with the tested POWERFUL/QUIET button. Identical transmissions do not reveal which feature state was active after each press. The next six carry `80 61 33 1A`. The next five alternate `80 68 F2 E0` / `80 52 33 0B` / `80 68 F2 E0` / `80 52 33 0B` / `80 68 F2 E0`. These identifiers differ from the published DKE and Indian-market examples above. Do not assign ON/OFF semantics to the alternating identifiers yet.

The final six frames alternate full-state payloads. In the user's reported iAUTO-X/COOL sequence, the candidate iAUTO-X state has B13=`09`, B14=`32`, B25=`80`, checksum=`63`; the returning COOL state has B13=`39`, B14=`3C`, B25=`00`, checksum=`1D`. All other bytes match. These encode AUTO/25 C versus COOL/30 C using the existing full-state interpretation. The observed B25 bit 7 change is associated with iAUTO-X here; it must not automatically be interpreted as a timer-slot selector outside verified timer contexts.

The user additionally reports that iAUTO-X returns to the previous operating mode on the next press, and that starting already in iAUTO-X can return to AUTO. This is a remote-display observation across modes, not a decoded result for every mode or a hardware-behavior test. The six captured full frames establish only the COOL/iAUTO-X alternation in this batch.

The user subsequently confirmed losing count during some groups and deliberately making an extra iAUTO-X press because the button felt uncertain. Extra captures are therefore plausible extra presses, not evidence of duplicate reception. This does not resolve attribution of the middle bursts or establish which planned group was absent. A focused repeat of ECONAVI and MILD DRY should use separate named capture windows; approximate spacing is sufficient, with no need to count seconds precisely while counting presses.

## ECONAVI and MILD DRY follow-up (2026-11-02)

The second capture contains exactly 14 complete, checksum-valid frames: five ECONAVI presses, five MILD DRY presses, then four explicitly labelled clarification presses. All passed Pronto length, timing, prefix and checksum validation. The clarification sequence starts at 18:55:31 Sydney time, over three minutes after the five-press groups ended at 18:51:55. No preparatory transmission was recorded in that gap; the remote's intervening local state is not inferred from the absence of IR.

| Remote action | Format | Observed encoding |
|---|---|---|
| ECONAVI ON | 16 bytes | Second section `02 20 E0 04 80 68 F2 E0` |
| ECONAVI OFF | 16 bytes | Second section `02 20 E0 04 80 52 33 0B` |
| MILD DRY ON | 27 bytes | B15=`7F`, checksum=`1C` in the captured COOL state |
| MILD DRY OFF | 27 bytes | B15=`80`, checksum=`1D` in the captured COOL state |

ON/OFF labels follow the user's explicit four-press sequence and remote-display observations. The five-press groups independently repeat ON/OFF/ON/OFF/ON. These are observed remote encodings, not proof of idempotent indoor-unit behavior under replay. MILD DRY frames differ only at B15 and checksum; do not simplify the `7F`/`80` change to a single bit or reuse the checksum for other operating states.

The user reports that MILD DRY automatically switches ECONAVI off and that the remote suppresses ECONAVI transmissions while MILD DRY is on. No extra ECONAVI-OFF short frame accompanies the captured MILD DRY-ON frames. Whether the full-state MILD DRY command also disables ECONAVI at the indoor unit remains a separate behavior test.

This identifies the first batch's alternating short-command burst as ECONAVI, with five valid captures. The identical `80 61 33 1A` burst is consistent with NANOE-G's position in the requested sequence (six captures), while no MILD DRY frame appears in that first batch. The reason for the missing first-batch group is not established. The focused repeat supplies five valid MILD DRY captures plus its labelled ON/OFF pair, so no further repetition is needed for this mapping.


## Diagnostic and hex-command interface

`panasonic_ac_protocol.h` classifies captures independently of climate-state decoding. DEBUG logs under
`panasonic_ir.protocol` include bytes, length, both section checksums and the recognised action or fields.
"Known encoding" means an exact supported transmit template or an identified RKR short action; it does
not mean every constant bit has a proven physical meaning. Other checksum-valid encodings are explicitly
marked as containing unknown fields. Valid full frames can update supported climate fields even when
additional fields are unknown. Invalid checksums, malformed captures and short commands never update
climate state. Transmitter echoes are logged before being suppressed.

A capture split by receiver idle timeout retains the existing second-section-only receive behaviour.
Its constant first section is reconstructed and explicitly labelled; that section's reported checksum
is therefore generated, not independently received. An isolated first section is incomplete. Malformed
captures log only successfully decoded whole bytes. Recognition requires the first four Panasonic bytes;
noise that prevents decoding that prefix is left to the existing raw/Pronto dumpers.

The log includes raw timer enables, 11-bit times and candidate update/slot fields. Their labels retain
model/context qualifications. Timer programming, individual cancellation and universal slot semantics
remain unimplemented. Current temperature is not decoded from these frames: it belongs to the climate
interface and can be supplied by an optional linked sensor. It is omitted from these protocol logs.

The native transmitter accepts an API string through this device-local action:

```yaml
api:
  actions:
    - action: send_panasonic_ac_command
      variables:
        data: string
      then:
        - lambda: |-
            id(ac).send_command_hex(data);
```

Accepted byte counts are 16 or 27 (complete command), 15 or 26 (final checksum omitted),
8 or 19 (second section only), and 7 or 18 (second section without checksum). Hex digits are
case-insensitive, contiguous or separated by whitespace at byte boundaries. Commas, `0x` prefixes,
odd nibbles, extra bytes and incorrect prefixes/section-type bytes are rejected. The first section must
be canonical when supplied. The final checksum is always recalculated, including when a supplied value
is wrong. Unknown action payloads are allowed for experimentation; valid framing cannot establish their
physical effects. Transmission logs show the final bytes. Raw sends do not publish climate state.

For example, `0220E00480B632` builds the verified 16-byte ALL CANCEL command with checksum `6E`.
This is a diagnostic API, not a timer-control entity. Keep the generic `send_pronto` action and receiver
`dump: all` for timing investigations. No hardware transmission or flashing is performed by unit tests.


## Experimental HA-owned slot 2 timers

The local implementation addresses ON2 and OFF2 only. Ordinary climate and vane writes still use
B13.3=0, regardless of desired timer state. There is deliberately no always-update switch yet.
No short independent timer programming command is established by our captures or the reviewed DKE
research. Timer writes therefore carry the component's current full operating state, including power,
mode, fan, temperature and vanes. They are not isolated timer-only commands. Remote-only features such
as MILD DRY/iAUTO-X are not retained by the normal climate encoder, and may be affected by a timer write.

The four new SET captures establish packing: ON1 06:00/OFF 23:30, ON2 17:00/OFF 23:30,
OFF1 23:30/ON 06:00, OFF2 02:10/ON 06:00. They decode clock candidates as 20:28, 20:27,
20:37 and 20:37. The remote clock was not recorded. The user's sequences added a second timer of the
same kind while the first was already configured; persistence of the first timer was not observed by
waiting for it to fire. Both candidate update flags are set in every SET capture. There is no captured
combined ON2+OFF2 write and no verified individual disable capture.

### Wire policy

Every explicit HA timer operation sends one full frame with both slot selectors set. B13 bits 1 and 2
carry desired ON2/OFF2 enables; bit 3 is set. B18 and B19.0..2 hold ON minutes. B19.4..7 and B20.0..6
hold OFF minutes. B19.3 and B20.7 are both set, matching the SET captures; independent effects are unknown.
B24 and B25.0..2 hold local clock minutes. B25.6 and B25.7 are both set; B25.3..5 are preserved from the
base operating frame. B26 is regenerated. Times are retained in the frame even when disabled: this is a
hypothesis for cancelling only the selected slot, not a verified cancellation command.

`encode_rkr_timers` patches just these fields and supports separate selectors/update flags for tests
and future policy changes. `RkrTimers` owns the HA state and always chooses slot 2 for both directions.
`PanasonicIrClimate::send_timer_state` refuses non-RKR models and any slot-1 selection. It does not
change the normal frame policy. No timer fields are imported into HA's desired state from remote IR.

### Controls, restoration and clock

The optional `panasonic_ir` controller takes `climate_id` and `time_id`. Its datetime and switch
platforms select `timer: ON2` or `OFF2`. The button platform exposes `apply_timers`.
The development YAML names switches "ON2 enabled (requested)" and "OFF2 enabled (requested)";
they advertise assumed state, not physical acknowledgement. Both default disabled. Initial times are
06:00 and 23:30. The two times and enables are saved together using the climate entity's preferences.
Normal ESPHome preference flush rules apply, so an immediate power loss can lose recent edits.

Changing either time or enable, including setting a disabled timer's time, sends both intended slots.
Applying unchanged values also sends, allowing a deliberate retry. Enabling does not choose a time
from the remote. Disabling one retains that timer's configured time and clears its enable bit while
applying the other HA slot exactly as remembered. The Apply button resends the same pair with a fresh
clock. Times must have zero seconds and minutes divisible by ten; unsupported values are rejected and
the existing value is republished, with no transmission. There is no silent rounding.

Restoration only publishes desired state. There are no boot, time-sync or periodic IR automations.
A write with an invalid clock saves intent but sends nothing; use Apply once the HA clock is valid.
The clock uses `RealTimeClock::now()` (local time including DST), at one-minute resolution, not rounded
to ten minutes. The development config explicitly uses `Australia/Sydney` with Home Assistant time.
DST changes are reflected in subsequent writes, not automatically sent to the unit; reapply after a
clock/DST change if the indoor unit does not adjust itself. The physical interpretation of the clock
field still needs a capture with the remote clock recorded.

### Hardware experiments required before relying on slot-1 isolation

1. With the ESP transmitter shielded, inspect the native timer logs and receiver capture: compare
   bytes/checksum for ON2-only, OFF2-only, both enabled and both disabled; record the remote clock in
   a fresh SET capture. Confirm ten-minute timer inputs and the minute-accurate local clock.
2. Arm an imminent slot-1 ON timer using the remote, then apply an enabled slot-2 pair. Wait for ON1
   to execute. Repeat for OFF1. An indicator or a beep alone cannot establish preservation.
3. Keep both slot-1 timers armed; disable only ON2, then only OFF2. Verify each selected timer stops
   executing, the other HA timer retains its intended behavior, and both remote timers still execute.
4. Compare a combined ON2+OFF2 frame with individual SET operations, including midnight and opposite
   orderings of ON/OFF times. Test initially disabled slots as well as already active ones.
5. Repeat from COOL, FAN, OFF and AUTO/iAUTO-X, recording mode/fan/vanes and feature indicators.
   B25.7 collides with our iAUTO-X candidate; do not infer feature isolation from its timer selector use.
6. Only if necessary, vary B19.3 and B20.7 one at a time while keeping other bytes fixed, and test
   whether the clock needs refreshing across DST. Do not enable automatic timer updates until the
   selected-slot and disable behaviors are established.

Slot 1 is expected to be unselected by the proposed encoding, but preservation is **not guaranteed**
by any current capture or unit test. Tests prove bytes and software behavior, not indoor-unit semantics.
