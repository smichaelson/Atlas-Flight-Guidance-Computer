# Happy Birthday — firmware 1.2.6 review

Date: 2026-09-20. Scope: one additional dashboard melody in Bringup and ServoBench.
These are three sequential author reviews, not independent hardware certification.

## Review 1 — firmware and melody

Checked the fixed command parser, append-only operation enum, shared buzzer owner,
25-note C-major sequence, 12-second timeline, tick wrap, cancellation, no overlap
and no replay. The existing Imperial March Preview C and startup arrays are
unchanged. Servo, ADC and buzzer drivers are unchanged. Host C tests exercise
both diagnostic profiles and all note/rest boundaries; malformed commands,
Stop, USB/DTR loss, session change, fault and driver failure are covered.

## Review 2 — dashboard and protocol

Checked the actual button command, confirmation, optional firmware capability,
strict metadata decoding, active-song label, Stop availability and disabled
controls during stale/demo/busy/PWM/update states. An older valid firmware
handshake retains Imperial March and disables Happy Birthday with an update
hint. The full Bringup suite passes, including 14 dashboard, 48 host/API/updater,
23 servo tests, both C console profiles and browser-logic regressions.

## Review 3 — build and release boundary

Both diagnostic images build and pass manifest, target, vector, bank-1 boundary
and ELF/HEX/BIN agreement checks. The change remains behind the existing
diagnostic profile boundary; it does not add a normal-flight command route.
Installation compares the complete source baseline and backs up every replaced
file. The repository check passes: 396 local links, 90 heading anchors, file
headers, artifact policy and RTOS source membership. Firmware version: 1.2.6.

| Image | Binary bytes | HEX SHA-256 |
|---|---:|---|
| Bringup | 216780 | `d807744e13459c05ddf91196f3facc00f84f23b67fe9dadd1e6cd55d03650dd0` |
| ServoBench | 219056 | `fc0cc09cb0d9ffa19ae6fbcd29690377dea629c7cbb9e457e435a207772cb3e5` |

## Live acceptance

The owner confirmed servos, motors and pyro loads unplugged and J5 open before
programming. The existing ServoBench profile was retained. The updater matched
the MCU UID, programmed the reviewed image and verified flash. On this run the
application returned over USB without a cold power cycle, and the new 1.2.6
handshake advertised the birthday capability, 25 notes and 12000 ms.

The installed dashboard was inspected in the browser. On the original 1.2.5
image, Imperial March remained available while Happy Birthday was disabled with
the update hint. On 1.2.6, the new button opened its confirmation and started the
physical buzzer. The first playback was interrupted using Stop indicators; the
board acknowledged Stop and subsequently reported playback off with zero Hz.

A second browser-started playback was observed through completion. Telemetry
contained every note index 1–25 and reported a 12000 ms interval between the first
playing sample and completion. Buzzer status, parser errors, decoder errors and
task faults remained zero. PWM, logic outputs and pyro armed state remained
zero throughout. The browser reported the correct song and progress, blocked
both song buttons during playback, and returned both buttons to Ready afterward.
No browser warnings or JavaScript errors were observed. The owner confirmed:
**“Yes, it sounds right.”**

This verifies one board's birthday playback and dashboard interaction, not flight
qualification. The [Ground Station guide](GROUND_STATION.md#play-the-buzzer-melody)
describes use.
