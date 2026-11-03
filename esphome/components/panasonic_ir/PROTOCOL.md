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
"Known encoding" means a supported transmit template (including recognised RKR ordinary-command timer enable bits) or an identified RKR short action; it does
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


## Independent HA timer operations (current implementation)

All four timers ON1, ON2, OFF1 and OFF2 are individually addressable. HA stores requested state,
not confirmed indoor-unit state, and does not import timer settings from received remote frames.
Ordinary climate and vane writes keep B13.3=0 and B23=89 regardless of the requested timers.
There is no automatic timer synchronization on climate changes, boot, clock sync or DST changes.

### Wire policy

Every explicit operation sends one full operating-state frame with B23=80 and B13.3=1.
The target type carries its requested enable and remembered time; disabling retains that time.
The opposite type always carries enable=1 and time=0x600, preserving its unknown existing state.
The opposite selector is kept at slot 1, matching the tested ON1/OFF1 preservation contexts.
The other slot of the target type is not selected. Hardware findings supporting this policy and
remaining limits are recorded below; unit tests cannot prove stored execution times are unchanged.

| Target | ON field | OFF field | B25 selector bits (excluding clock) |
| --- | --- | --- | --- |
| ON1 | Requested time/enable | Preserve: enable=1, 0x600 | 00 |
| ON2 | Requested time/enable | Preserve: enable=1, 0x600 | 40 |
| OFF1 | Preserve: enable=1, 0x600 | Requested time/enable | 00 |
| OFF2 | Preserve: enable=1, 0x600 | Requested time/enable | 80 |

B18 and B19.0..2 encode ON minutes; B19.4..7 and B20.0..6 encode OFF minutes.
B19.3 and B20.7 are always 1 in generated independent operations, matching the remote. They are
**unknown timer-adjacent flags**, not established update flags. Raw encoding/decoding helpers retain
both values for capture regression tests; the climate transmission entry point rejects cleared flags
and requests that do not preserve exactly one opposite timer type.
B24 and B25.0..2 encode current local clock minutes. B25.3..5 retain the normal frame values.
B26 is recalculated after all fields, including B23, are set.

`make_rkr_timer_operation` builds the independent-operation policy; `encode_rkr_timers` packs raw
fields; `RkrTimers` owns requested state. This separates protocol packing from the HA policy.
The HA operation builder accepts only valid ten-minute target times, never special target times,
so cancellation does not use the untested ON cancellation variant with 0x600.

These are full frames carrying current climate power/mode, temperature and vane/fan state.
Independent timer programming with short frames has not been established. Unknown remote-only
operating settings, such as MILD DRY/iAUTO-X, can still be affected by full-frame transmission.

### Controls, restoration and clock

The datetime and switch platforms accept `timer: ON1`, `ON2`, `OFF1` or `OFF2`. A time change or
switch write applies only that slot, even when its value is unchanged (allowing an explicit retry).
A time change while disabled sends cancellation with the new remembered time. Times must be valid
HH:00/10/20/30/40/50 with zero seconds; invalid inputs are rejected and the previous time republished.
Switches expose assumed/requested state; labels in the example YAML make this distinction explicit.
Default times are 06:00 for ON slots and 23:30 for OFF slots, all disabled.

The button platform has optional `apply_on1`, `apply_on2`, `apply_off1`, `apply_off2` and
`apply_timers` entries. The existing `apply_timers` key is retained, now labelled **Apply All HA Timers**
in the example. It schedules ON1, ON2, OFF1, OFF2 as four separate operations, one per one-second
scheduler interval. The first callback follows the scheduler's normal initial interval offset.
The sequence uses current requested settings and the local clock at each step; repeated Apply All
restarts/coalesces the pending sequence. An individual apply removes that slot from a pending batch.
The interval is cancelled after completion or failure; invalid clock aborts the rest without retry.
The spacing is an implementation choice awaiting hardware validation, not a protocol guarantee.

Preferences use a new four-slot layout and migrate the old ON2/OFF2 intent when no valid new layout
exists. ON1/OFF1 start disabled on migration. Restoration and migration do not send or queue IR.
An invalid clock prevents transmission but leaves requested settings saved for a later explicit apply.
The configured Home Assistant clock uses `now()` in local time (Australia/Sydney in the example),
including DST. Clock minutes are not rounded to timer resolution; seconds are not transmitted.

TX logs show both selected slot fields, requested times/enables or explicit preservation, local clock,
B13/B19/B20/B23/B25, unknown flags and the entire frame. RX logs retain raw values and unknown-field/
checksum checks; identifying a timer-command context does not mark every byte as understood.

The following sections retain the hardware evidence and earlier combined-slot experiment history.

## Controlled ON1 preservation/cancellation experiment (2026-11-03)

The head unit initially had ON1 programmed and its timer indicator lit. An original-remote OFF command
with ON1 enabled left the indicator lit. The user then covered the remote and cancelled its timers,
so the head unit did not receive that cancellation. A second OFF command, now with the remote's ON
flag clear, extinguished the head unit's timer indicator.

Preserved (indicator remained lit):

```text
02 20 E0 04 00 00 00 06 02 20 E0 04 00 6A 36 80 AF 0D 00 0E E0 00 00 89 00 00 59
```

Cancelled (indicator went out):

```text
02 20 E0 04 00 00 00 06 02 20 E0 04 00 68 36 80 AF 0D 00 0E E0 00 00 89 00 00 57
```

These valid frames differ only in B13.1 and the resulting checksum. Both have B13.3=1,
ON/OFF time fields 0x600, clock 0x000 and B23=89. In this tested ON1 context, setting the ON
flag preserved the existing timer and clearing it cancelled it. In an additional test where SET was
shielded and no ON timer had been programmed in the indoor unit, an ordinary OFF with ON enabled,
0x600 time and zero clock did not activate a timer. The special time did not replace the retained ON1
time. These are reported hardware observations; OFF1 symmetry and effects on other slots are unverified.

The special value is independently named `kPanasonicAcTimeSpecial = 0x600` in
[IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Panasonic.h).
"No new timer time supplied" is a useful interpretation of these ON1 observations, not a proven
universal definition. It is not a normal minutes-since-midnight value and does not imply disabled.

Explicit ON1 SET capture at 12:15:20:

```text
02 20 E0 04 00 00 00 06 02 20 E0 04 00 6A 36 80 AF 0D 68 89 D7 00 00 80 DF 02 0B
```

The genuine capture encodes ON=06:00, OFF=23:20 and raw clock=0x2DF (735 minutes, 12:15),
with valid checksum 0B. The clock field does not include the captured seconds (:20).
The earlier handover contained a transcription error, CE instead of DF; that was not a hardware
capture error. The CE 02 0B combination is retained only as an intentionally corrupted negative
checksum test (calculated checksum FA). Unlike the ordinary OFF,
this SET has B23=80 and actual timer/clock fields. That pair alone did not isolate B23; the later
controlled OFF2 cancellation experiment below does.

Diagnostics now recognise ON/OFF enable bits within the otherwise exact RKR ordinary transmit template.
This is a field-classification allowance, not proof that OFF1 behaves symmetrically. Every other byte,
reserved bit and checksum is still checked. Valid unmodelled encodings retain the unknown-field
classification, including the valid SET capture. The intentionally corrupted variant is rejected
for its checksum. A zero clock matching the ordinary RKR timer-field pattern is explicitly reported as
not supplying the remote's current clock, rather than asserting midnight. Other clock values remain
raw candidates: a zero alone cannot distinguish a placeholder from genuine midnight.

### Effect on HA slot 2 and next experiments

No transmission policy changes follow from this evidence. Normal climate and vane commands keep
B13.3=0, including after hearing either ordinary OFF or SET from the remote, and never copy remote
enables into HA's desired slot-2 state. The new experiment isolates B13.1 with B13.3 held at one; it
does not independently prove B13.3's meaning. Earlier hardware tests remain the basis for clearing it.

At this stage the experimental implementation sent both slot-2 selectors and real times for both types.
That policy is superseded by the independent-operation policy above. The B23 correction below remains.
The ON1 result with special time does not prove how disabling a selected slot with a real time works.

Retain the planned slot-1 isolation, combined slot-2, individual disable and iAUTO-X tests. Add:
(1) the equivalent controlled OFF1 experiment; (2) extend the now-completed B23 cancellation comparison
to timer programming; (3) compare special-time and real-time enable/disable behavior
with both previously programmed and absent timers; (4) record the displayed remote clock alongside
SET and ordinary OFF captures, including a genuine midnight SET if practical. Only perform these
hardware experiments with user approval. C++ tests establish classification, packing and preservation
policy; they do not simulate or prove indoor-unit timer effects.


## Controlled B23 and selective OFF2 cancellation experiments

The user programmed OFF1 with this command (checksum omitted; the transmitter calculated it):

```text
02 20 E0 04 00 00 00 06 02 20 E0 04 00 6D 36 80 A1 06 68 89 D7 00 00 80 91 03
```

Then this experimental OFF2 cancellation preserved OFF1:

```text
02 20 E0 04 00 00 00 06 02 20 E0 04 00 69 36 80 A1 06 68 29 D8 00 00 80 90 C3
```

Changing only B23 from 80 to 89 (with a recalculated checksum) cancelled OFF1. This isolates
B23's effect in this tested command, but does not establish meanings for its individual bits.
Original-remote timer SET/CANCEL captures consistently use 80; ordinary commands use 89.

Additional B23 values were tested with the other command fields unchanged:

| B23 | Reported hardware result |
| --- | --- |
| 80 | OFF1 preserved |
| 81, 82, 88, 90, A0, C0, 01, 02, 04, 08 | OFF1 cancelled |
| 84 | No beep on repeated attempts; apparently ignored; programming also unsuccessful |

The B13=69, B23=80, B25=C3 cancellation was tested with three initial configurations:

| Initially enabled | Timer indicator after cancellation |
| --- | --- |
| OFF1 only | Remained on |
| OFF2 only | Went out |
| OFF1 and OFF2 | Remained on |

The single-slot tests strongly support selective OFF2 cancellation preserving OFF1. The shared
indicator in the combined test does not independently establish every stored slot's state.
B25=C3 is therefore not necessarily invalid. Combined ON2/OFF2 operation remains unverified.

### Earlier combined-slot implementation (superseded)

At this stage, explicit HA timer operations now set B23=80 in the shared timer encoder, before the checksum.
This covers enable/disable, time changes and Apply HA Timers. Ordinary RKR climate and vane
transmissions keep B23=89 and B13.3=0. Other models and model defaults are unchanged.
Both slot-2 selectors remain set: B25 is C3 for the tested 0x390 clock; its low three bits still
encode the current local clock's high bits, so the complete byte varies with the clock.
RX diagnostics label B23=80 with B13.3=1 as the observed timer-command context, without treating
that byte alone as proof that all other fields are understood. Unknown-field/checksum checks remain.
Controls, restoration without transmission, time validation and local/DST clock handling are unchanged.

Still verify on hardware: programming OFF2; programming ON2; enabling both together; disabling
one while preserving the other; preserving remote-owned slot-1 timers across slot-2 operations;
and absence of unexpected mode or other operating-state changes. Successful HA timer programming
is not established by the cancellation results. The bit-level meaning of B23, update flags and
any iAUTO-X interaction remain unresolved. Tests verify bytes and software behavior, not these
physical effects. No further hardware commands are authorised by this implementation change.


## Independent preservation findings

With B23=80 and B13.3=1, hardware tests now support:

| Enable | Timer field | Observed effect |
| --- | --- | --- |
| 0 | Valid time | Cancel selected timer |
| 0 | 0x600 | Cancel selected OFF timer (ON variant not tested) |
| 1 | Valid time | Enable/program selected timer |
| 1 | 0x600 | Preserve existing timer without supplying a new time |

OFF1 set by the remote survived an ON2 cancellation carrying OFF enable=1/time=0x600, with
B20.7 either zero or one. ON1 set by the remote survived an OFF2 cancellation carrying ON
enable=1/time=0x600, with B19.3 either zero or one. An absent OFF timer did not become enabled
from enable=1/time=0x600. Earlier ordinary-command tests also found no activation of an absent ON
timer with the special value. Preservation is not programming and 0x600 is not midnight.

OFF programming, cancellation and preservation worked with either value of B20.7; ON preservation
worked with either value of B19.3. Their bit-level purpose remains unknown. All examined original
remote SET/CANCEL and ordinary captures have both bits set, so production operations keep them set.
Previously used labels claiming timer-update flags came from DKE research and are not RKR conclusions.

Slot selection is ON1/ON2 via B25.6 and OFF1/OFF2 via B25.7. The selective OFF2 cancellation results
above support leaving OFF1 unselected; the shared indicator cannot verify every stored timer time.
The new implementation replaces simultaneous ON2/OFF2 writes with individual operations and explicit
opposite-type preservation. General independence across every state combination still needs validation.

### Next hardware validation

- Program and cancel each of ON1, ON2, OFF1 and OFF2; record complete TX logs and actual execution.
- Verify the stored execution times of all three non-target slots, including remote-programmed timers.
- Test all four simultaneously and Apply All's four-frame sequence, including its one-second spacing.
- Check execution across midnight and daylight-saving transitions, with no automatic resynchronization.
- Check full-frame timer updates when the AC has operating settings unknown to HA.
- ON cancellation with a special target time remains untested and is not used by this implementation.

No new raw capture bytes were supplied for these preservation experiments. Tests assert the specified
packing and software policy, alongside existing genuine capture fixtures; generated frames are not
labelled as physical captures. Hardware actions require separate user approval.
