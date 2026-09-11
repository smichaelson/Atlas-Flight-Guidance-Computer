# ServoBench 1.2.5 — direct angle moves

September 11, 2026. Three sequential author reviews; these are not independent reviews or flight qualification.

## Result and earlier findings

Every accepted position selection now writes its destination pulse once, allowing the KST's internal controller to handle motion. The 600 µs/s software ramp is removed. This follows the owner's observation that enabling at 1520 µs moved quickly and smoothly, while ordinary moves stuttered. A new regression failed on the installed 1.2.4 ramp and passes on the direct implementation. Physical acceptance of 1.2.5 is recorded separately below.

The dashboard offers −50°, −25°, 0°, +25° and +50° buttons and whole-degree numeric entry. One click, Move or Enter sends one target; typing only previews. There is no streaming slider, motion timer, retained target queue, retry or keepalive. The KST factory nominal mapping is 1000/1500/2000 µs for −50°/0°/+50°. Enable retains 1520 µs and displays its nominal +2° honestly. Models display commands, not measured shaft angles.

The earlier connector correction is retained: PCB PWM 1 at the right corresponds to old schematic net PWM7; the complete right-to-left mapping is 7,8,1,2,3,4,5,6. One route table governs pin selection, compares and shutdown. The owner physically confirmed PWM1. Other connectors have software-model coverage, not individual electrical verification.

The earlier 1.2.2 output-owner loop accidentally applied its normal 8.4 V policy after the ServoBench 8.55 V policy. That was corrected and covered by a failing-before/passing-after owner-loop regression. An unloaded 1.2.3 test then stopped at an actual retained 8559 mV ADC sample. The owner approved 16-sample hardware averaging, delivered in 1.2.4. This averaging and the exact 8550/8551 mV boundary remain in 1.2.5.

Later slider fixes corrected a browser timer receiver error and reduced polling payloads. The owner still reported jerky dragging, so those observations are not counted as a physical smooth-motion success. The obsolete slider/scheduler has been replaced completely. The approved Preview C melody and its 42 notes / 16.5 seconds are unchanged.

## Review 1 — firmware, routes and shutdown

Reviewed the 1.2.4-to-1.2.5 source delta and all paths into the output owner. SET retains command age, cancellation/output epochs, USB session, voltage/fault checks, one-channel activation and configured pulse bounds. It writes the compare and both reported pulse fields directly. Unlike initial enable, an active SET does not reset CNT, issue EGR.UG, remux the pin or restart the timer. The bundled HAL enables compare preload for each TIM1/TIM3 PWM channel; the next PWM period adopts the destination.

Strict C regressions pass for all eight expected physical routes, direct forward/reverse targets, steady holds, 1000/1500/2000 µs positions, narrower windows and invalid bounds. Timer counter, update-event, channel-enable and running-control registers remain unchanged during SET. The new direct-target assertion fails on the old ramp. Stop-before-queued-enable/move, USB epochs/loss, zero/invalid/stale ADC, reference/monitor failure, 8550 accepted / 8551 stopped, 3 s idle including tick wrap and 30 s session expiry still pass. The complete owner loop also passes. Normal flight policy tests retain their original voltage and permission requirements.

## Review 2 — dashboard and host

The 23 inert servo host/protocol tests pass, including one request per full-range target, identity/control epochs, delayed HTTP commands crossing Stop, strict boolean `servo_direct` capability and legacy Stop compatibility. Both host and UI require the direct capability, corrected layout, 16-sample averaging and 8550 mV policy before enable/move. Old telemetry remains viewable; Stop remains available.

Production UI handlers pass inert DOM tests for all 101 whole-degree conversions, numeric preview, exactly one command per selection, no timer/queue/replay, in-flight rejection, and fresh/visible/voltage/pending/batch/session gates. Actual browser preview checks show +50° → 2000 µs and −12° → 1380 µs with no JavaScript errors. These preview checks do not actuate hardware. The full Bringup and normal service suites pass, including real console serialization, USB core/owner, storage and expansion boundaries, and buzzer controls.

## Review 3 — builds and release consistency

ServoBench and ordinary Bringup build and pass manifest/HEX/BIN/vector/profile checks. Normal Debug and Release also build successfully. The first normal Debug post-link attempt lacked the ARM objcopy directory on PATH; after adding the installed toolchain directory both normal builds completed. No source workaround was needed.

| Artifact | Bytes | SHA-256 of HEX |
|---|---:|---|
| Atlas-ServoBench 1.2.5 | 218548 | `2e7b638ba81cd0ca3666da2d28deeea4e0932e3e07400f87c4190ea39bbdb3c7` |
| Atlas-Bringup 1.2.5 | 216272 | `eeca38861f057f5431791aaec7ad4258bcd1f52e84652a479f87fa055ef754f5` |

ServoBench uses 122200 bytes of DTCM and ordinary Bringup 122184 bytes. Normal Debug uses 205864 flash bytes / 90136 DTCM bytes; Release uses 106236 flash bytes / 90112 DTCM bytes. Builds and models do not establish maximum live stack use or electrical qualification.

The installer checks all 437 protected source/image files against the previous installed manifest, backs up every changed file and verifies installed hashes. Existing uncommitted work is retained. The console delta was checked to contain only direct-capability metadata and the updated servo reply, preserving the accepted melody exactly. No commit or push is part of this work.

## Live acceptance and portability

1.2.4 previously passed an unloaded eight-channel command test and a narrow physical PWM1 repeat in which the owner reported smooth movement and steady holds. This did not establish smooth wider slider control; the later failures above remain part of the record.

1.2.5 was programmed and verified on the matched board UID `00330032 33335105 38393631`, DFU serial `200364500000`. ROM 0x92 accepted the application-start request but USB did not return; the owner cold-restarted battery and USB, then the host confirmed live `servo_bench` 1.2.5 with `servo_direct:true`. All eight unloaded channels accepted 1000/2000/1500 µs targets and Stop, followed by an actual 3 s idle cutoff. After the owner reconnected the free servo to PWM1, the actual browser buttons commanded −25°, +25°, 0°, −50°, +50°, 0°, then Stop. The owner reported “Yes—quick and smooth.” Accepted targets equalled reported pulse destinations throughout the captured active frames, with no ADC/task faults, and the final mask and all pulse fields were zero. This is physical motion acceptance on PWM1, not a calibrated angle or oscilloscope measurement. Portable extraction checks are recorded in the release package result. Factory-ROM application restart is not reliably qualified: a verified flash may need a battery-and-USB cold restart with BOOT0 LOW. Do not repeat a verified flash solely because USB did not return. Only the unrestricted PWM1 servo is available for physical motion checks. No oscilloscope waveform, calibrated full-travel measurement, all-eight-connector electrical test or second physical laptop result is claimed.

During the physical button sequence, the ADC recorded two PWM-supply readings below 4.8 V: 2407 mV at MCU tick 263434 and 2367 mV at tick 264144, during the +25° and 0° commands. The 3V3 reading stayed near 3.35 V and no ADC or task fault was reported. These are telemetry observations, not scope-confirmed rail minima or durations; supply transient behavior remains unqualified. The owner-selected voltage policy accepts usable nonzero readings below 8.55 V, so no new low-voltage gate was added.

See [the servo procedure](SERVO_BENCH.md) and [portable Windows setup / updating](GROUND_STATION.md).
