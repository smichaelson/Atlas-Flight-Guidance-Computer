# Radio link 1.4.0 review and installation record

Scope: two connected Atlas boards, factory RFD900x serial settings, bench communication
only. The user requested at least three reviews. These are focused review passes by
the implementing agent, not independent certification or RF/electrical qualification.

## Review 1 — protocol and evidence

Reviewed frame boundaries, ownership of UIDs/challenges, CRC validation, timeout boundaries,
tick wrap, duplicate/self/foreign replies, simultaneous tests and bounded response traffic.
Connection depends on a matching round trip, never UART-write success or inbound PING alone.
Expired proof is permanently cleared by service, preventing revival after a full tick wrap.
The parser has no command-dispatch path. Tests cover every split point, each damaged byte,
each truncated prefix, arbitrary command text, replay, late ACK, transmit error, peer loss,
monitoring, concurrent two-way tests and response flooding. Passed.

## Review 2 — integration and preservation

Reviewed sole UART ownership, the asynchronous USB completion and queue backpressure,
USB epoch fencing, IMU/service timing, stabilization gates, saved configuration and DFU.
The owner continues sensor work while waiting for RF. Only the three bounded radio
diagnostic operations are exempted from the stabilization maintenance block; local AT
and other maintenance remain blocked. Radio test results are published before USB
completion and cannot be replayed into a new USB session. Production console tests cover
waiting, timeout, successful peer detail, full result queue, disconnect and maintenance
denial. Existing servo, stabilization/persistence, buzzer, IO, USB, storage, expansion and
driver tests passed. A new console test initially assumed a zero storage counter after
earlier tests; corrected it to assert the counter is unchanged, then the suite passed.

## Review 3 — release, dashboard and hardware

Reviewed the final diff, generated USART3 configuration, CMake/IAR source membership,
capability/schema compatibility, dashboard freshness and result presentation, all four
build profiles, frozen image identity and physical target selection. Completed the
installation and hardware checks below on 2026-09-27. Passed.

This pass found two dashboard issues and corrected both: **Stop monitoring** needed the
existing HTTP deliberate-action flag, and historical timeout counts must not label an
intentionally stopped monitor as a current failure. Regression tests exercise both.
Actual dashboard Connect, Test, Stop and restart operations were checked against device
responses. The refreshed Communications page shows the responding peer and a successful
test while the peer runs on battery alone.

## Build and regression results

- Normal Debug, normal Release, Bringup and ServoBench builds passed. USART3 is 57600
  in both generated C and CubeMX; other UART configurations are unchanged.
- Bringup suite passed, including pure-C radio/parser and production console tests,
  nine new Python radio tests, browser presentation tests and existing dashboard,
  ground-station, servo, stabilization, persistence, boot, IO and storage regressions.
- Host driver and service suites passed, including actual USB class/owner, expansion,
  storage and IO implementations. Final radio UI regressions passed after the two
  display/control corrections.
- Repository documentation/source checks and whitespace checks passed.
- Installed ServoBench image uses 239848 bytes of flash and 123672 bytes of DTCM,
  leaving 7400 bytes within the 128 KiB DTCM region.

## Device inventory before installation

| USB port | MCU UID (decimal words) | Previous firmware | Saved stabilization |
|---|---|---|---|
| COM3 | 3342386 / 859001093 / 943273521 | ServoBench 1.3.4 | Enabled, calibrated, reverse mask 15, SD present |
| COM4 | 3407904 / 859001093 / 943273521 | ServoBench 1.2.5 | Not supported by old firmware; SD absent |

Both boards reported SW2 OFF, PWM off, pyro disarmed and SD unmounted before installation.
The owner also confirmed both SW2 switches OFF. COM3 calibration was preserved while
stabilization was temporarily saved OFF for the existing DFU interlock; its prior enabled
state was restored afterward and verified to reload after a later power cycle.

## Installation and hardware results

Both MCU UIDs were checked through the application and again through the ROM bootloader
before programming. Both flash writes verified against the same frozen ServoBench
1.4.0 image. Following the requested full power cycle, both applications enumerated and
reported version 1.4.0 with the radio-link capability. Startup was established by these
application observations, not merely by the programmer's start request.

Installed HEX SHA-256:

```text
7c7f2b85587fa0a2cb4d9fe5ca49a6c27e1c94b7549db6495a8f037d8c54ae88
```

| Check | Observed result |
|---|---|
| COM3-initiated tests | 10/10 acknowledged by COM4; 68–241 ms round trips |
| COM4-initiated tests | 10/10 acknowledged by COM3; 90–300 ms round trips |
| Healthy paired runs | No UART errors, receive drops, invalid frames, transmit failures or test timeouts |
| Sensor/task continuity during those runs | Maximum observed IMU age 10 ms on COM3 and 6 ms on COM4; no task faults or power events |
| Peer powered off | Explicit connect returned a 2000 ms timeout; dashboard showed no peer response and remained responsive |
| Peer restored, battery only | COM4 absent from Windows USB ports; automatic monitoring recovered, and explicit test 63 received its ACK in 75 ms |
| Dashboard Stop and restart | Stop cleared monitoring/connection proof; restart received peer ACK in 176 ms |
| COM3 saved stabilization | Enabled, calibrated, saved, reverse mask 15; preserved across the later power cycle |
| Output state | SW2 OFF, PWM off, pyro disarmed and logic outputs low throughout the radio checks |

COM3 also restarted during the peer-power-off exercise, so this run establishes timeout
and recovery behavior, not an exact measured connected-to-disconnected latency. The
continued monitoring during the deliberate outage accumulated 31 expected timeouts;
these remain historical counters after recovery, not current link failures.

Local evidence is retained under `build/radio-140-*`: preflash inventory, programmer
verification, per-board replies/telemetry, peer-off status, battery-peer recovery and
final dashboard state. Build/test logs use `build/radio-*-tests.log` and
`build/radio-*-build.log`; the final UI capture is `build/radio-dashboard-connected.png`.

Existing functionality was checked through the regression suites, source review and
preserved device settings. Outputs remained off; servo motion and pyro hardware were
not requalified during this radio installation. These are bench communication results,
not an RF range, electrical or general flight qualification.
