# Module Bring-up and Acceptance

## Scope and safety boundary

This procedure validates communication and basic formatting for the module drivers. It does not calibrate sensors, validate coordinate frames, qualify RF performance, prove navigation accuracy, or authorize flight/energetic outputs.

Keep the physical event-arm link open. Connect no igniter, motor, servo, or energetic load. Use a current-limited bench supply, SWD debugger, ESD controls, and the exact board revision under test.

## Required equipment

- Atlas Flight Computer Rev. 0.1 or a revision whose differences are explicitly reconciled.
- Current-limited supply and a meter or oscilloscope for rail/reset verification.
- SWD probe with access to `atlas_board`, the file-local `atlas_rtos` context, FreeRTOS task state, and health counters.
- Logic analyzer capable of I2C, SPI, and UART decoding at the configured rates.
- GNSS antenna with a suitable view of sky, when testing fixes/PPS.
- A known-compatible NINA-B112 firmware version and a paired BLE central.
- A locally legal, preconfigured RFD900x pair, attenuated or operated into a suitable test setup.

## Pre-power checks

1. Confirm board identity, revision, assembly variant, and visible rework.
2. Verify supply polarity and set a conservative current limit.
3. Confirm event outputs are unarmed and all external powered-output headers are disconnected.
4. Confirm SPI chip-selects are high and BNO/BLE resets are low before firmware releases them.
5. Record toolchain, binary hash, source revision, probe firmware, and ambient conditions.

## Bring-up sequence

### 1. Boot and startup report

Flash a Debug image and break immediately after `AtlasBoard_Init()`. Inspect every field in `atlas_board.init`; do not rely only on LED color.

Expected results:

- LED transitions blue to green when all checks pass, or yellow when one or more checks fail.
- Buzzer remains electrically silent.
- No event-output gate changes state.
- The watchdog is initialized only after module probing finishes.
- `AtlasWatchdog`, `AtlasIO`, `AtlasControl`, and the idle task are created from static storage; scheduling begins only after IWDG starts.
- Every failed entry has a matching health counter, bus trace, or timeout explanation.

Green is only a startup-transaction summary. In particular, the external-radio entry proves that USART3 reception started; it does not prove an RFD900x is attached or linked.

### 2. Direct sensors

Follow the acceptance section in each sensor guide. Capture at least one complete identity/configuration transaction and at least 100 sequential samples. Verify:

- bus mode, clock, address/chip select, and byte order;
- identity or PROM CRC result;
- requested configuration readback;
- update/data-ready rate within tolerance;
- stationary values are physically plausible and do not stick, saturate, or repeat unexpectedly;
- unplugged, held-reset, or forced-NACK cases return within the documented timeout.

### 3. BNO085

Capture reset release, interrupt assertion, the first SHTP exchanges, product IDs, feature commands, and decoded reports. Confirm the interrupt is active low and that service drains it without an interrupt storm. Compare a slow hand rotation against rotation-vector continuity; this is a communication test, not an orientation-frame acceptance.

### 4. GNSS and PPS

Record `software_version`, `hardware_version`, the `CFG-VALSET` ACK, the complete RAM-layer `CFG-VALGET` response, a valid NAV-PVT frame, and PPS captures. Confirm all six configuration keys equal the requested values and `configuration_mismatches` remains zero. Verify the parser rejects a deliberately corrupted recorded frame in host tests. With a valid time solution, successive PPS periods should center near 1,000,000 microseconds; document oscillator/timer tolerance and any outliers.

### 5. BLE

Confirm both switch lines remain high through reset, RTS/CTS toggle correctly, `AT`, `AT+CGMM`, and `AT+CGMR` return terminal `OK`, and no NVM-write command occurs during ordinary boot. Test `AtlasBle_ConfigureSps(..., false)` first and retain the name, role, SPS-server, and start-mode readbacks plus the accepted `AT&D1` response. `AT&D1` is set-only; prove its effect later with the wired DSR transition and a new successful `AT` exchange.

Then verify both transitions with a paired central: `AtlasBle_EnterDataMode()` must receive terminal `OK` for `ATO1`, wait at least 50 ms, and carry a bidirectional binary payload; `AtlasBle_EnterCommandMode()` must drive the module DSR input low-to-high, receive the transition's unsolicited terminal `OK`, and then receive a second terminal `OK` for its explicit `AT` probe. Do not substitute a debugger edit of `command_mode` for this test.

Persistent configuration (`AtlasBle_ConfigureSps(..., true)`) writes NVM, powers the module down, resets it, enters command mode through the wired DSR transition, repeats all four supported profile readbacks, and returns to Data mode with `ATO1`. Retain that entire transcript, then independently power-cycle and repeat the wired command-mode/readback check before calling persistence bench-verified. An approved hardware recovery path is required before this maintenance action.

### 6. RFD900x

First prove the MCU and radio serial baud agree using transparent loopback or a paired modem. The generated USART3 default is 115200; an unconfigured RFD unit commonly starts at 57600. Do not send `+++` until application traffic has stopped and one second of link silence is independently observed. Read `ATI` and `ATI5`; photograph or retain the settings, then exit with `ATO`. If testing an S-register write, use a benign approved parameter, require `ATSn?` to equal the staged value, and leave `persist=false` until an explicit recovery procedure exists. Do not change RF settings as part of a basic communication test.

### 7. LED and buzzer

Exercise all eight LED combinations and verify MCU-high turns the corresponding color on. For the buzzer, scope PE5 and PE6 before listening: both must be 4.8 kHz, approximately 50 percent duty, and opposite phase. Stop must leave zero differential waveform.

Use `AtlasRtos_SubmitCommand()` after scheduler start. Direct `AtlasLed_*()` or `AtlasBuzzer_*()` calls are permitted only in a deliberate pre-scheduler driver test; they violate runtime ownership from an application task.

### 8. RTOS runtime and supervision

Follow [RTOS bench acceptance](RTOS_ARCHITECTURE.md#rtos-bench-acceptance) while all outputs remain inert. At minimum:

1. Confirm HAL milliseconds and the FreeRTOS tick both advance at 1 kHz after scheduling and do not disturb measured GNSS PPS period.
2. Observe increasing `AtlasRtosSnapshot.sequence`; require all required validity bits and advancing per-device timestamps. Cross the two-second startup-grace boundary without `SENSOR_STALE`; never call a driver from the debugger/application.
3. Measure I/O-cycle and complete snapshot/hook period/jitter under simultaneous maximum planned BNO085, GNSS, RFD900x, and BLE traffic. Confirm no healthy application cycle reaches 10 ms.
4. Exercise every RTOS command, correlate ticket/type/status, and verify queue-full behavior is bounded and visible.
5. Fill each RTOS RX stream deliberately; verify existing bytes remain ordered, new drops are counted, and the test framing layer detects/resynchronizes after loss.
6. Capture task stack high-water marks through startup, peak traffic, command-mode waits, error handling, and the future control workload in both Debug and Release.
7. For each long mode transition, confirm `maintenance_active` is published before the yielding call, control is inhibited, a second long transition is rejected during `sensor_recovery_active`, all required timestamps recover within 1.2 seconds, and no GNSS/UART/SHTP loss is hidden.
8. Stall and overrun `AtlasControl`, stall `AtlasIO`, exceed a declared mode-transition deadline, withhold each required sensor stream, force a sensor sample error, and use an instrumented test build to exercise the stack-margin decision. Confirm the first `AtlasRtosFault` latches permanently and IWDG refresh stops.
9. Measure watchdog reset time and confirm event/output pins remain benign through the complete fault/reset/reboot sequence.

## Fault-injection minimum

Repeat affected portions with:

- each removable/external module absent;
- one forced UART baud mismatch;
- an injected invalid GNSS checksum;
- a full UART software ring in host tests;
- an SPI identity byte forced wrong;
- an MS5611 PROM CRC mismatch;
- BNO085 interrupt held inactive;
- BNO085 asynchronous reset after reports are enabled; confirm `runtime_fault` latches and watchdog refresh stops;
- a GNSS `CFG-VALGET` response with one unequal or missing key;
- BLE data-mode traffic followed by the wired DSR command-mode transition;
- an `AtlasIO`-serviced UART recovery event;
- a command queue at capacity and both RTOS receive streams at capacity;
- an application-hook stall and 10 ms overrun, I/O-task stall, expired declared long-I/O deadline, each required stale sensor, and each task's stack-margin rejection;
- back-to-back long mode-transition requests; confirm the second returns `ATLAS_ERROR_BUSY` until the fixed recovery window ends;
- repeated apparently healthy cycles after a latched RTOS fault; confirm watchdog refresh never resumes.

Every wait must terminate and no failure may activate a hazardous output.

## Evidence record

Copy this block into the issue, pull request, or retained test report:

```text
Test ID:
Date / operator:
Board revision / serial number / rework:
Source revision and binary SHA-256:
Compiler / build configuration:
Power source and current limit:
Connected equipment and firmware versions:
Procedure step(s):
Expected result:
Observed result:
Health counters before / after:
Captured files or trace links:
Pass / fail / blocked:
Anomalies and disposition:
```

Bench verification is complete only when all failures are explained and the evidence is reviewable by another qualified person.
