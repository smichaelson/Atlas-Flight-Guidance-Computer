# Atlas Flight Guidance Computer

Atlas is a custom STM32H743ZIT6 flight-computer and data-acquisition board. This repository contains the Rev. 0.1 hardware reference package, generated STM32 platform code, and a statically allocated FreeRTOS firmware layer for every fitted sensor, GNSS, BLE, the external RFD900x radio header, the RGB LED, and the differential buzzer.

The firmware baseline is designed to remove device-register, framing, checksum, and mode-transition work from future estimation, guidance, control, logging, and telemetry code. It is not a flight controller yet.

> [!WARNING]
> This is development firmware, not flight-qualified software. It has compiled and passed deterministic host tests, but the module drivers have not yet completed the documented tests on an assembled Atlas board. Keep the event-arm link open, connect no energetic device, motor, or flight load, and use current-limited bench power.

## What is ready

- Typed, timeout-bounded drivers for ADXL375, LSM6DSV16B, MMC5983MA, MS5611, and BNO085.
- The official CEVA SH-2/SHTP stack pinned and adapted for the BNO085.
- A checksum-validating u-blox UBX parser, NAV-PVT decoding, PPS capture, and RAM-only NEO-M9N configuration with exact `CFG-VALGET` readback.
- Allocation-free receive-to-idle UART transports for GNSS, BLE, and the external radio.
- NINA-B112 identity, exact SPS profile readback, explicit persisted post-restart verification, verified command/data-mode transitions, and transparent data access.
- Guarded RFD900x/SiK command entry, transparent data access, identity/settings reads, and S-register write/readback support.
- Active-high RGB LED control and opposite-phase TIM15 buzzer drive.
- One `AtlasBoard` composition layer, startup report, interrupt routing, health counters, and a latched runtime fault path.
- FreeRTOS 10.6.2 with static allocation only, one exclusive hardware-I/O owner, a 100 Hz application hook, coherent multi-sensor snapshots, queued module/output commands, and buffered radio/BLE receive streams.
- A highest-priority supervisor that checks startup/runtime status, required-sensor freshness, I/O/application liveness and deadlines, bounded maintenance operations, and every task stack margin before it alone refreshes the independent watchdog.
- Clean Arm GNU Debug and Release target builds plus deterministic host protocol tests.

## What is deliberately not claimed

- No sensor is bench-verified, calibrated, or assigned a validated body-frame transform yet.
- No RF link budget, coexistence, range, regulatory, or antenna validation has been completed.
- The J9 long-range radio is an external RFD900x/SiK frequency-hopping modem, not a LoRa transceiver.
- There is no flight/mission state machine, estimator, controller, durable logging format, telemetry schema, or event-output controller.
- Task periods and initial stack allocations are implemented but have not yet been measured under worst-case hardware traffic; they are not real-time guarantees until the RTOS bench procedure passes.
- A green startup LED means the software's bounded startup transactions passed. It does not prove a paired radio link, GNSS fix, BLE connection, sensor accuracy, or flight readiness.
- Hardware interaction cannot be guaranteed by source review or compilation alone. Each module must pass its recorded bench acceptance procedure on the exact board and module firmware revision.

## Start here

1. Read [Project status](docs/PROJECT_STATUS.md) for the exact implemented and unimplemented boundary.
2. Read [Engineering standards](STANDARDS.md) before changing firmware, generated setup, or safety-relevant behavior.
3. Use the [Module firmware index](docs/modules/README.md) to find the driver, default configuration, example, health counters, and acceptance test for a device.
4. Read the [RTOS architecture and application contract](docs/RTOS_ARCHITECTURE.md) before adding control code, tasks, queues, or driver access.
5. Review [Firmware architecture](docs/FIRMWARE_ARCHITECTURE.md) for the complete layering and failure model.
6. Follow [Building](docs/BUILDING.md), then [Module bring-up](docs/BRINGUP.md) on unarmed hardware.
7. Consult [Validation status](docs/VALIDATION.md) before treating a capability as verified.

## Repository map

| Path | Purpose |
|---|---|
| `App/Inc`, `App/Src` | Project-owned public APIs, drivers, transports, board composition, RTOS tasks/policy, and scheduler-aware timing |
| `Tests/host` | Deterministic protocol/math tests and minimal HAL mocks |
| `ThirdParty/CEVA/sh2` | Pinned official BNO085 SH-2/SHTP implementation and retained notice |
| `ThirdParty/FreeRTOS-Kernel` | Pinned STM32CubeH7-matched FreeRTOS 10.6.2 subset, GNU/IAR Cortex-M7 ports, and MIT license |
| `Core` | STM32CubeMX-generated entry point, peripheral setup, interrupts, and HAL integration |
| `FATFS`, `USB_DEVICE` | Generated storage and USB integration scaffolding |
| `Drivers`, `Middlewares` | ST/CMSIS, USB, and FatFs dependencies used by the target build |
| `cmake`, `CMakeLists.txt`, `CMakePresets.json` | Arm GNU build configuration |
| `EWARM` | IAR Embedded Workbench project and linker configurations |
| `Atlas.ioc`, `.mxproject` | STM32CubeMX configuration and generation metadata |
| `hardware` | Schematic PDF, BOM, and candidate manufacturing outputs; no raw KiCad project |
| `docs/modules` | One operational guide for each sensor/module/output |
| `docs/reviews` | Recorded communication-baseline reviews plus three RTOS-specific review passes |

## Firmware startup

`main()` initializes generated MCU peripherals, calls `AtlasBoard_Init()`, and only then starts the independent watchdog. Board initialization continues after individual failures so `atlas_board.init` contains a complete report.

The visible sequence is:

1. LED blue while bounded module probes run; buzzer remains silent.
2. Direct sensors are reset/identified/configured and critical settings are read back.
3. BNO085 product IDs and default volatile reports are established.
4. GNSS identity is polled, RAM-only 10 Hz NAV-PVT configuration is applied, and every key is read back.
5. The RFD900x UART transport starts without sending `+++` or changing the modem.
6. BLE is reset in normal boot mode and identified without saving settings.
7. LED becomes green only if every attempted startup step returned `ATLAS_OK`; otherwise it becomes yellow.
8. IWDG starts, all RTOS resources/tasks are created from static storage, and scheduling begins.
9. The I/O task services every module, samples every direct sensor, publishes coherent snapshots, executes queued commands, and is the only post-start driver owner.
10. After a two-second acquisition grace, the watchdog supervisor refreshes IWDG only while startup/service/sampling statuses, required sensor ages, task heartbeat/deadline checks, maintenance deadlines, and stack margins all remain healthy.

Inspect `atlas_board_startup_status`, `atlas_rtos_start_status`, the file-local `atlas_rtos.health`, `atlas_board.init`, `atlas_board.runtime_fault`, and each module's `health` structure in a debugger. LED color is a startup summary, not diagnostic evidence.

## Build and test

With CMake 3.22 or newer, Ninja, and Arm GNU Toolchain on `PATH`:

```text
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
cmake --build --preset Release
```

On Windows, deterministic host tests require a C11 host GCC:

```text
powershell -ExecutionPolicy Bypass -File Tests/host/run_tests.ps1
```

Run the read-only repository/documentation review under PowerShell 7 (`pwsh`) as well:

```text
pwsh -NoProfile -File Tests/repository/check_repository.ps1
```

See [Building](docs/BUILDING.md) for exact validated tool versions, memory use, IAR status, and troubleshooting.

## Source authority

When sources disagree, stop and resolve the conflict rather than guessing:

1. The assembled board and confirmed as-built records govern physical hardware.
2. `hardware/Atlas-schematic-rev-0.1.pdf` governs intended connectivity for this snapshot.
3. Current manufacturer data sheets and interface manuals govern device behavior.
4. The compiled source set, active linker script, and build definitions govern runtime behavior.
5. `Atlas.ioc` governs intended generated configuration only after its generated diff is reviewed.
6. Markdown explains the baseline but does not override measured hardware or compiled code.

Editable KiCad files and the obsolete `Atlas_Origins` material are intentionally absent. The manufacturing package remains a candidate until reconciled with the exact as-built order.

## Contributing

- State the affected hardware, safety impact, expected behavior, and retained evidence in every functional pull request.
- Keep generated changes distinguishable from project-owned `App/` changes.
- Add new sources to both CMake and IAR unless support is explicitly retired; keep each build on its matching FreeRTOS Cortex-M7 port.
- After scheduling begins, extend snapshots/commands instead of calling a driver from application code or a new task.
- Run the required builds/tests and update the relevant module guide in the same change.
- Require qualified review for clocks, memory placement, interrupts, watchdog behavior, powered outputs, event channels, and fault handling.

The detailed requirements are in [STANDARDS.md](STANDARDS.md) and the pull-request checklist under `.github`.

## Licensing

No project-level license has been selected. Do not assume redistribution or reuse rights beyond permission from the project owner. Vendored CMSIS, STM32 HAL, USB, FatFs, CEVA, and FreeRTOS sources retain their original license notices.
