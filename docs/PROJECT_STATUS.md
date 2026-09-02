# Project Status

- **Status reviewed:** 2026-09-01
- **Hardware reference:** Atlas Flight Computer schematic Rev. 0.1, dated 2026-03-19
- **MCU baseline:** STM32H743ZIT6; STM32CubeMX 6.17.0; STM32Cube FW_H7 1.13.0
- **Firmware maturity:** RTOS-integrated, target-built, and host protocol/policy-tested; physical module and real-time acceptance pending

This page states only what is demonstrably present. It is not a flight-qualification statement or a promise that untested hardware will behave correctly.

## Implemented baseline

### Board and transports

- Project-owned `App/` layer with common status values, bounded SPI transactions, allocation-free UART receive rings, module health counters, and `AtlasBoard` composition.
- Complete startup report retained at `atlas_board.init`; initialization continues after individual module failures so later faults remain visible.
- Minimal ISR routing for UART receive/errors, BNO085/LSM6 interrupts, and GNSS PPS capture. Protocol parsing and bus I/O remain in `AtlasIO` task context.
- Runtime service errors latch in `atlas_board.runtime_fault`. The watchdog is refreshed only when startup succeeded and the runtime latch remains clear.

### RTOS runtime

- FreeRTOS Kernel 10.6.2 from the matching STM32CubeH7 1.13.0 package, using native APIs, Cortex-M7 r0p1 GNU/IAR ports, 1 kHz preemption, and static allocation only. No `heap_*.c` or linked `pvPortMalloc` is present.
- Three required tasks: exclusive hardware I/O owner, 100 Hz application/control hook, and higher-priority watchdog supervisor; plus the static idle task.
- The I/O task continuously services every initialized protocol, samples all four direct sensors, captures all configured BNO085/GNSS reports, drains radio/BLE payload into bounded RTOS streams, and publishes mutex-coherent snapshots.
- Application code has no direct driver ownership. It reads `AtlasRtosSnapshot`, submits bounded commands for LED/buzzer/radio/BLE, and can consume serialized transparent RX streams.
- IWDG is refreshed only by the supervisor after startup/service/sampling status, required-sensor freshness, I/O/application heartbeat and deadline checks, long-operation deadline, prior-fault latch, and all task stack margins pass.
- A two-second startup grace is followed by explicit required-sensor age supervision. Long BLE/RFD mode transitions publish a control-inhibit state, have fixed deadlines checked during and after the call, and permit one non-renewable 1.2-second sensor-backlog recovery window.
- Driver delays yield the current task after scheduler start while preserving HAL delays during pre-scheduler startup. Current peripheral ISRs call no RTOS API; SysTick alone enters the kernel through the reviewed tick wrapper.

### Sensors

- **ADXL375:** identity, standby configuration, 4-wire format, selectable ODR, critical-register readback, data-ready poll, coherent XYZ read, and nominal scaling.
- **LSM6DSV16B:** identity, software reset, high-performance accelerometer/gyro configuration, full-scale/ODR/readback, INT1 data-ready routing, unusual Z/Y/X output-map handling, and scaling.
- **MMC5983MA:** reset, product identity, bandwidth readback, packed 18-bit decode, bounded magnetic/temperature conversions, automatic SET/RESET, and explicit offset-cancelling SET/RESET pair.
- **MS5611-01BA03:** reset, complete PROM read, invalid-PROM rejection, CRC-4 validation, selectable OSR conversions, and first/second-order 64-bit compensation.
- **BNO085:** reset/interrupt/I2C/timer adapter to the pinned official CEVA SH-2 stack, product-ID verification, report configuration, decoded callbacks, bounded service, and fail-visible I/O/protocol/decode/reset counters.

### Communications and feedback

- **NEO-M9N GNSS:** UBX framing/checksum, noise resynchronization, MON-VER identity, NAV-PVT decode, 1 MHz PPS capture, ACK correlation, and RAM-only rate/protocol/message configuration followed by exact CFG-VALGET readback.
- **NINA-B112 BLE:** normal-mode reset straps, RTS/CTS UART, model/firmware identity, strict bounded AT transactions, SPS/peripheral configuration with readback, explicit NVM persistence followed by reset and complete profile re-verification, wired DSR command-mode entry, `ATO1` data-mode entry, and transparent bytes.
- **External RFD900x:** transparent USART3 transport, one-second SiK escape guards, bounded local AT engine, identity/settings queries, S-register write/readback, optional save, and explicit host-baud reconfiguration. No command is sent automatically at boot.
- **RGB LED:** explicit active-high transistor-gate control for all eight binary combinations.
- **Buzzer:** silent default, 1-10 kHz opposite-phase TIM15 channels, continuous tone, and nonblocking bounded beep.

## Validation completed without hardware

- Arm GNU 14.3.1 Debug and Release RTOS images compile and link without warnings.
- Host C11 tests compile with `-Wall -Wextra -Werror` and emulate the direct-register contracts for ADXL375, LSM6DSV16B, MMC5983MA, and MS5611; they also cover NAV-PVT/checksum/configuration/PPS behavior, BLE profile/mode/persistence/overflow behavior, substantive RFD900x identity/settings plus S-register verification, LED GPIO ownership, differential buzzer timing, and UART overflow accounting.
- CMake and IAR source membership include every project-owned, CEVA, and required FreeRTOS source; IAR XML parses successfully.
- Host tests cover tick/sample-age rollover and supervisor behavior when required data is reported stale, including the bounded maintenance exception, in addition to every watchdog policy gate and the existing device protocols. The required-sensor bit/age mapping also received source review.
- All 19 project-owned firmware/integration translation units pass strict extra warnings and GCC static analysis. Unchanged generated USB/FatFs code is outside that stricter claim.
- Six recorded review passes cover the communication baseline plus RTOS architecture/interrupts, concurrency/build/failure behavior, and documentation/onboarding/repository hygiene. See `docs/reviews/`.

Exact evidence and the distinction between target-built and bench-verified are in [Validation status](VALIDATION.md).

## Required physical validation

No module is yet marked bench-verified. Before application algorithms rely on it, complete the named procedure in its guide and retain board revision, module firmware, traces, observed values, and fault-injection results.

Key pending work:

1. Confirm every fitted part and assembly/rework state against the Rev. 0.1 schematic and actual board.
2. Capture SPI/I2C/UART/reset/interrupt transactions for every module and compare them with the relevant manufacturer document.
3. Establish and verify a versioned body-frame transform for each inertial/magnetic sensor.
4. Characterize sensor rate, latency, noise, saturation, calibration, thermal behavior, and stale-data deadlines.
5. Verify NEO-M9N fix/PPS behavior, NINA firmware/connection behavior, and RFD900x paired-link/baud/RF behavior.
6. Scope LED polarity and both buzzer pins; confirm silent reset and opposite phase under load.
7. Repeat timeout, NACK, held-reset, ring-overflow, unexpected-reset, and reconnect/recovery tests on inert hardware.
8. Measure RTOS task rates/jitter, snapshot ages, worst-case execution time, command/RX load, and task stack high-water marks under simultaneous traffic.
9. Fault-inject I/O/application stalls, a 10 ms application-cycle overrun, long-operation deadline expiry, every required stale-sensor bit, low stack margin, sensor failures, and watchdog reset; retain fault codes and safe-output waveforms.

## Not implemented

- Flight or mission state machine, estimator, control algorithms, or command authorization.
- Validated real-time requirements, measured worst-case task execution/jitter, approved stale-data deadlines, or qualified task stack margins. The implemented periods and guards are an integration baseline, not timing qualification.
- Versioned telemetry/application framing above the RFD900x and BLE byte streams.
- Durable microSD logging, power-loss recovery, capacity policy, or log schema.
- USB CDC command/security policy.
- Calibrated ADC rail/continuity interpretation.
- Actuator profiles or a reviewed event-output arming/firing controller.
- Persistent reset-cause record, crash dump, or operational fault manager beyond the conservative runtime latch/watchdog behavior.
- Continuous integration, hardware-in-the-loop automation, release signing, or a project-level license.

## Important operational boundaries

- J9 is only an interface for an external RFD900x. A successful startup proves the MCU UART transport started; it cannot prove that a modem is installed, powered, paired, frequency-compatible, or legal to transmit.
- Ordinary boot never writes GNSS nonvolatile memory, BLE nonvolatile memory, or RFD900x settings.
- After scheduling begins, the I/O task exclusively owns every driver. Calling a driver from application code is outside the supported concurrency contract even if it appears to work in a debugger.
- BLE/RFD mode transitions are maintenance-only. Control code must inhibit while `maintenance_active` or `sensor_recovery_active` is set and must not attempt to chain long transitions through the recovery window.
- `AtlasBle_ConfigureSps(..., true)` and `AtlasRfd900x_SetParameter(..., true)` are explicit maintenance actions that write module NVM.
- The long-range radio is not LoRa; RFD900x uses SiK firmware and a frequency-hopping serial link.
- The current firmware does not touch event-output gates after generated safe initialization and does not authorize energized testing.

## Hardware and manufacturing provenance

The Rev. 0.1 schematic PDF, BOM, and a coherent candidate Gerber/drill/assembly package are retained. Editable KiCad files and obsolete origin-history material are intentionally excluded. The candidate manufacturing package has not been independently proven to match the exact as-built order; do not reorder it without reconciliation and review.

## Updating this status

Update this page in the same pull request that changes an implemented capability, validation state, supported toolchain, hardware revision, safety limitation, or manufacturing provenance. Use the controlled vocabulary in [Validation status](VALIDATION.md); a compile does not qualify as a bench test.
