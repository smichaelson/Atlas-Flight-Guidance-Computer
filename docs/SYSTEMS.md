# Systems and readiness

## Read this first

The code now contains integration paths for every named onboard subsystem and the expansion buses. **That is software implementation, not proof that every physical system is online.** Host tests cannot establish board wiring, real-time margins, module firmware compatibility, RF connectivity or safe energetic operation.

Major interfaces: `AtlasRtos_GetSnapshot()` for sensors, `AtlasIo_GetSnapshot()` for analog/inputs/output state, and the copied command/result APIs in [Peripheral services](PERIPHERALS.md). The [review report](REVIEW_REPORT.md) owns finding IDs and test evidence.

## Select the right profile

| Build preset | Startup and use |
|---|---|
| **Bringup / BringupRelease** | Isolated diagnostic RTOS application; USB JSON dashboard, explicit staged probes and SD tests. Failed GNSS alone permits deliberate manual recovery; other probes remain once per boot. RGB, PWM and pyro are inhibited. No control hook; missing modules are reported without the normal required-sensor reset loop |
| Debug / Release | Normal application framework below: eager device probes, required-sensor supervision, optional services, qualified output APIs and algorithm hook |

Use **[startup](startup.md)** for the new PCB. It centralizes power/BOOT/reset/USB DFU, fixtures, dashboard and expected evidence. Diagnostic software coverage includes sensors, GNSS/PPS, BLE, SD/RTC, voltage/input monitoring, buzzer, bounded logic GPIO and optional radio/expansion tests. RGB outputs are deliberately unavailable because of the confirmed Q6-Q8 PCB defect. Power regulation and connector waveforms still need instruments; PWM/pyro need separate inert qualification. The PDF/USB manufacturing-polarity conflict is an as-built inspection gate before USB commissioning.

## Capability matrix — normal application

| System | Stock behavior and owner | Remaining acceptance |
|---|---|---|
| ADXL375 | SPI2 identity/configuration; 400 Hz device ODR, nominal 2 ms polling; RTOS snapshot | Fitted part, scaling/axes, saturation, timing and shared-bus integrity |
| LSM6DSV16B | SPI3 identity/configuration; accel/gyro ready checks; coalesced INT1 plus 5 ms fallback | Paired sample timing, interrupt routing, axes and expansion coexistence |
| MMC5983MA | SPI2 identity, automatic SET/RESET on-demand field reads every nominal 50 ms | Magnetic calibration/interference and physical conversion timing |
| MS5611 | I2C1 PROM/CRC and compensated D2/D1 reads at OSR1024 every nominal 100 ms; minimum waits corrected | Actual conversion timing, pressure reference, thermal behavior and plausibility |
| BNO085 | I2C1 `0x4A` SH-2/SHTP identity plus four reports; edge-gated two-phase reads, finite discovery and reset-first shared-I2C recovery | Owner reports version 1.0.1 follow-up streaming successfully; retain long-run/error/shared-bus evidence and qualify fitted firmware, calibration, axes and timing |
| NEO-M9N GNSS/GPS + PPS | USART stale-RX preflight; UBX identity before PPS; RAM-only 10 Hz NAV-PVT configuration/readback; bounded parser recovery; explicit failed-probe retry and stage/HAL diagnostics | Flash 1.0.2 and prove receiver UART/configuration, antenna, fix/accuracy/UTC quality and measured PPS |
| RFD900x at J9 | USART3 raw transport; queued mode/identity/settings/parameter/host-baud operations | Exact SiK firmware, regulated power, antenna, lawful RF settings and bidirectional peer test |
| NINA-B112 BLE | USART6 RTS/CTS; reset/identity; queued SPS configuration and mode transitions | u-connectXpress version/features, advertising, SPS client, CTS/DTR behavior and reconnect |
| RGB LED | **Hardware-blocked on rev-0.1.** Confirmed Q6-Q8 terminal mismatch; PB6/PB7/PD14 are forced low, all nonzero APIs are unsupported, startup colors/UI controls are removed, and inhibit telemetry is mandatory | Approved transistor-footprint rework or corrected PCB revision, unpowered connectivity review, then current-limited electrical/optical qualification before any separately reviewed re-enable |
| Buzzer | Differential TIM15 drive; queued beep/stop and owner-serviced expiry | Differential waveform/acoustic behavior; expiry can extend during a sensor-owner stall |
| microSD/FatFs | Dedicated low-priority owner; explicit remount, read, append/sync/close; active-low mechanical card detect | Real card enumeration/full/corrupt media, removal, power loss, filesystem durability and throughput |
| USB CDC | Dedicated VBUS-aware owner; validated control requests, retained TX and bounded RX; no DMA | Actual enumeration/host interoperability, cable/power/suspend/reset/timeout behavior; product VID/PID authorization |
| ADC / rails / continuity | Dedicated output owner calibrates ADC1/3; ten scaled ranks, separate VREF/temperature conversions, validity and continuous snapshots; retained ADC3 phase/channel/raw/HAL diagnostics | The 2026-09-04 version 1.0.1 log shows `start=OK` but a latched `IO` before any sample, with no external-ADC error count. Capture the new 1.0.2 diagnostic fields and diagnose the ADC3 reference/temperature path before accepting reference/dividers/settling/tolerances, diagnostic-current limits, thresholds or physical arm proxy |
| PWM1–8 | TIM1/TIM3 at nominal 333 Hz, 1 us units; explicit KST calibration/enable/update/disable APIs | KST X10 V8.0 exact limits, each linkage's neutral/travel, rail overshoot/load and 3.3 V signal margin |
| GPIO1–7 / external switch | Continuous raw input snapshot; qualified masked outputs; default low | Input polarity/debounce/application meaning and load/voltage compatibility |
| Pyro1–5 | Disarmed by default; qualified software-arm + measured armed-feed gate; serialized 500 ms pulses, minimum 500 ms OFF, three retries maximum | Inert waveform/fault testing, independent safety review, physical thresholds, energy isolation and load/circuit qualification |
| UART4 / I2C2 / external SPI3 | Raw copied transaction APIs in the shared sensor owner; no discovery traffic | Attached-device driver, protocol, electrical compatibility, bus load and actual signal timing |
| RTC / fault monitoring | Explicit UTC set; valid FAT fallback when unknown; PVD/ECC/fatal faults inhibit outputs and expose diagnostics | RTC retention/clock accuracy, brownout/ECC behavior and watchdog reset timing on actual silicon |

All physical acceptance entries remain **pending**. The normal application's radio/BLE/USB streams have no supplied authenticated remote-control parser, flight telemetry schema or flight-state machine. The separate diagnostic USB schema is a physical bench interface only. Raw expansion APIs cannot configure an unspecified external device.

## Default versus deliberate actions

Ordinary boot initializes/probes sensors and links, starts monitoring, attempts read-only SD mount and permits VBUS-gated USB enumeration. It does not append a log, transmit application RF data, enable PWM, drive general GPIO high, arm/fire pyro, or save module settings.

Deliberate application requests are needed for SD appends/remount/UTC, USB data, radio/BLE commissioning, expansion transactions and qualified outputs. Request acceptance is distinct from completion; completion is distinct from physical success. Every result consumer must keep draining its bounded queue.

The conservative output gate requires healthy required sensors, fresh ADC data, no latched fault, and no maintenance/recovery interval. It does not judge GNSS fix suitability, sensor calibration, flight phase, RF authentication or mechanical safety; those remain application/release responsibilities.

## Outputs and the owner's requirements

The supplied pyro policy uses one nominal **500 ms** pulse and at least **500 ms OFF** before another attempt. A one-millisecond tick-phase guard and fresh post-interval ADC observation make actual retry spacing conservative. Persistent qualified continuity permits **at most three retries after the initial attempt**.

The maximum is **four launch attempts per channel per boot**, not four per command. Disarm/rearm, another manual request, or configuration changes do not restore attempts. A reset starts a new disarmed boot; it is not authorization to repeat a firing sequence. One channel at a time is allowed.

J5 connects the armed supply physically. There is no independent digital arm-link contact sensor: qualified armed-feed voltage is only a supply-present proxy. Without that voltage, continuity is UNKNOWN, not OPEN. It is monitored while disarmed as well as armed once qualified settings exist. Loss/ambiguity/staleness cannot authorize another pulse. Restoration does not automatically software-arm.

KST's [X10 V8.0 technical specification](https://cdn.shopifycdn.net/s/files/1/0570/1766/3541/files/X10_V8.0_Technical_Specifcation_ee6acc4d-79de-4d61-b686-3288dc5d4153.pdf?v=1704951839) supports a 333 Hz, 900–2100 us signal envelope and 4.8–8.4 V supply. Its working-frequency line uses 1520 us while its position table uses 1500 us at zero: **the firmware therefore requires a measured per-channel neutral instead of choosing one**. The schematic's nominal 8.4 V rail is already at the servo's published upper supply limit; overshoot and tolerances must be resolved before connection.

See [output configuration and qualification](PERIPHERALS.md#output-configuration-and-qualification) for the full contract. Qualification booleans are assertions by the application author, not measurements or certificates. The stock image supplies no qualified profile.

## Acceptance gates

Keep energetic loads and servos disconnected during these checks. Output acceptance begins with inert loads, an oscilloscope/logic analyzer, current limits and an approved procedure; this document is not permission to energize a connected system.

| Gate | Evidence to retain before accepting the subsystem |
|---|---|
| Build and source | Fresh Debug/Release, warnings, binary/map hashes, source membership, tests; actual IAR build if that toolchain will be used |
| Startup and sensors | Real board-order identity/configuration, continuing plausible samples, minimum conversion delays, calibration/axes and failure behavior |
| RTOS and memory | Scheduled-release latency and end-to-end response under combined load; stack/call-chain margins; DMA addresses/ECC/cache policy; ISR and watchdog behavior |
| SD/RTC | Known expendable media; read/append/sync/readback, full/corrupt/absent cases, rapid removal/replacement without implicit remount, power-loss uncertainty and UTC retention |
| USB and links | Host enumeration/control/data/ZLP, reset/unplug/reconnect/CTS stalls, copied-buffer lifetime, no stale replay; approved radio/BLE firmware, configuration/readback and peer exchange |
| Analog and inputs | Independent meter comparison, all ten rank mappings, reference/temperature, clipping and stale-data rejection, continuous arm/continuity transitions with no energetic load |
| PWM and GPIO | Scope all eight frame/pulse widths and first enable/disable; qualified servo limits and rail margin; raw input mapping/debounce; reset/fault states and pending-command invalidation |
| Pyro safety | Inert channel routing, measured 500 ms pulse and OFF interval, four-attempt ceiling across rearm/manual commands, ongoing continuity, physical-link loss, abort/DMA error/stall/debug/reset/brownout and non-overlap |
| Release | Qualified independent reviewers, unresolved-risk disposition, approved flight/application logic and traceable responsible-owner release decision |

TIM6+DMA drives both pyro edges without task progress. It still shares MCU clocks, bus, DMA, GPIO and power: **it is not a redundant external hardware cutoff**. Firmware cannot rule out transistor faults, shorts, frozen peripherals or loss of energy isolation. Continuing continuity also cannot prove successful deployment.

## What passing tests do not mean

The tests exercise real code with explicit boundary models; they are not a board simulator. ADC samples, timer/DMA events, RTC responses and USB low-level completions are injected. The complete FreeRTOS kernel, actual BNO085 startup, radio peers, card hardware and electrical outputs are not exercised by those tests.

No flight-control algorithm, actuator linkage calibration, power-loss-safe logging format, cryptographic command authorization, bootloader, board fabrication approval or flight qualification is supplied. Follow the [review report](REVIEW_REPORT.md) and [engineering standards](DEVELOPMENT.md), not historical “all pass” labels.
