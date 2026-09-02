# Building Atlas Firmware

Atlas carries an Arm GNU CMake build and an IAR Embedded Workbench project. A clean build proves source/toolchain consistency only; it does not prove electrical behavior, sensor correctness, or flight readiness.

## Validated baseline

The 2026-09-01 review used:

| Tool | Version / status |
|---|---|
| Arm GNU Toolchain | 14.3.1 (`arm-none-eabi-gcc`, Arm build 14.174) |
| CMake | 3.26.4; project minimum is 3.22 |
| Local target generator | MinGW Makefiles with GNU Make |
| Repository preset generator | Ninja 1.11.1 |
| Host-test GCC | MinGW-w64 GCC 13.1.0 |
| IAR EWARM | Project membership/XML inspected; compiler unavailable, so no IAR binary was produced |

Clean Arm GNU builds completed without compiler warnings:

| Configuration | Flash used | DTCM used | Result |
|---|---:|---:|---|
| Debug | 146,344 bytes (6.98%) | 51,976 bytes (39.65%) | compiled and linked without warnings |
| Release | 74,064 bytes (3.53%) | 51,976 bytes (39.65%) | compiled and linked without warnings |

These clean-build values use the documented MinGW Makefiles fallback. They are evidence for this source snapshot, not fixed limits. Retain fresh size/map output with every compiled change.

All 19 project-owned `App/Src` and generated integration translation units (`main.c` and `stm32h7xx_it.c`) also passed `-Wall -Wextra -Werror` and GCC `-fanalyzer`. That stricter statement does not extend to unchanged generated USB/FatFs or third-party code, which has upstream extra-warning findings. The normal complete target builds remain clean under the repository `-Wall` baseline.

## Arm GNU with repository presets

### Prerequisites

- CMake 3.22 or newer.
- Ninja.
- Arm GNU Toolchain binaries on `PATH`: `arm-none-eabi-gcc`, `arm-none-eabi-g++`, `arm-none-eabi-objcopy`, and `arm-none-eabi-size`.
- A clean checkout path; do not reuse another machine's CMake cache.

Confirm tools before configuring:

```text
cmake --version
ninja --version
arm-none-eabi-gcc --version
```

Build both configurations from the repository root:

```text
cmake --preset Debug
cmake --build --preset Debug --clean-first

cmake --preset Release
cmake --build --preset Release --clean-first
```

Preset output is placed under `build/Debug` and `build/Release`. Generated build directories, ELF/HEX/BIN files, maps, objects, and stack-usage files are ignored and must not be committed.

### Generator fallback

Ninja is the supported preset generator. If a local environment requires another CMake generator, use separate output directories so preset state is not mixed:

```text
cmake -S . -B build/ArmDebug -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ArmDebug --clean-first
```

Use the corresponding `Release` build type and a different directory for Release. Record the generator in test evidence.

## Deterministic host tests

The host suite builds selected project-owned protocol/math modules as strict C11 with `-Wall -Wextra -Werror`. On Windows with host GCC available:

```text
powershell -ExecutionPolicy Bypass -File Tests/host/run_tests.ps1
```

The suite currently covers:

- ADXL375 identity, exact startup registers, data-ready status, burst ordering, signed conversion, and nominal scaling;
- LSM6DSV16B identity/reset completion, exact control and interrupt registers, ready flags, temperature/gyro data, and the device-specific Z/Y/X accelerometer register map;
- MMC5983MA identity, bandwidth readback, 18-bit XYZ packing, on-demand field conversion, and temperature conversion;
- MS5611 reset/PROM command flow, CRC-4 acceptance/rejection, the manufacturer compensation example, and completed sample accounting;
- u-blox NAV-PVT offsets and checksum rejection;
- GNSS CFG-VALSET ACK correlation plus CFG-VALGET exact-readback acceptance/rejection and counter semantics;
- coherent GNSS PPS snapshots and period calculation through 32-bit timer wrap;
- BLE AT/profile transactions, mismatch rejection, fail-closed response-buffer overflow, wired command/data transitions, and the persisted post-restart re-verification path;
- RFD900x substantive identity/settings responses plus echo-tolerant SiK S-register write/readback acceptance and mismatch rejection;
- LED ownership transfer from CubeMX TIM4 staging, fail-dark initialization, and logical color output;
- buzzer TIM15 ownership, opposite PWM modes, frequency bounds, continuous stop, and wrap-safe timed stop;
- UART receive-ring overflow accounting.
- RTOS periodic scheduling across the 32-bit HAL tick rollover;
- watchdog-policy acceptance for healthy heartbeats, supervised long I/O, and adequate task stacks;
- fail-closed watchdog-policy results for startup/service/sampling faults, I/O/application stalls, expired long-I/O deadlines, and low/zero stack margin.
- sensor-timestamp rollover and supervisor acceptance/rejection when required data is reported fresh/stale, including the narrowly bounded exception for an active maintenance transition; the per-sensor required-bit/age mapping is source-reviewed rather than host-executed.

The executable is emitted only to the operating-system temporary directory. A passing host suite does not replace the module bench procedures.

## Repository and documentation checks

Run the read-only repository review after documentation or file-layout changes:

```text
pwsh -NoProfile -File Tests/repository/check_repository.ps1
```

PowerShell 7 is required because the checker uses modern .NET path normalization. It resolves local Markdown links; requires the onboarding, RTOS, and review documents; checks project-owned Doxygen file/function tags; rejects raw KiCad sources, Atlas_Origins paths, target build artifacts, and FreeRTOS heap implementations; rejects direct App-driver `HAL_Delay()` use and unreviewed ISR-to-kernel calls; and verifies every App source plus the required kernel/port sources and definition in CMake and IAR. It does not fetch external URLs or replace technical review.

## Build outputs to inspect

For every functional change, retain and review:

- compiler version and complete warning output;
- Debug and Release memory summaries;
- `Atlas.map`, particularly flash/DTCM placement, task/static-object growth, stack/heap symbols, and unexpected retained code;
- symbol output proving exactly one `SVC_Handler`, `PendSV_Handler`, `SysTick_Handler`, and `xPortSysTickHandler`, with no linked C or FreeRTOS heap allocator;
- compiler `.su` output plus measured FreeRTOS stack high-water marks; static estimates do not replace runtime worst-case measurement;
- source membership for both build systems;
- host-test result and any relevant bench trace.

The current implementation uses static driver and RTOS storage and links no FreeRTOS heap implementation. Task stacks, UART rings, RTOS receive streams, and BNO085/snapshot caches account for most of the DTCM footprint.

## IAR Embedded Workbench

1. Open `EWARM/Project.eww`.
2. Select the `Atlas` configuration and confirm the active linker file.
3. Rebuild the entire project rather than using copied objects.
4. Treat every warning as a review item.
5. Review the IAR map for flash/RAM placement, stack, heap, and any future DMA buffers.

The project lists `App/Inc`, every `App/Src` unit, CEVA SH-2, FreeRTOS common sources, and the IAR Cortex-M7 r0p1 `port.c` plus `portasm.s`. It defines `ATLAS_USE_FREERTOS=1` and includes the IAR port directory. XML/source-membership inspection passed, but an actual IAR compile remains required before IAR can be called validated.

## Programming and first boot

- Use SWD, current-limited bench power, and an open physical event-arm link.
- Attach no igniter, motor, servo, flight battery, or energetic load.
- Break after `AtlasBoard_Init()` and inspect every field in `atlas_board.init`.
- After scheduler start, inspect `atlas_rtos.health`, snapshot sequence/validity/timestamps, task stack high-water marks, and all driver counters. Application code must not call a driver directly.
- A yellow LED indicates one or more startup failures; green indicates software transactions passed, not that the system is flight-ready.
- The independent watchdog starts only after bounded startup probes. Only the supervisor refreshes it, and only while startup/service/sampling status, required-sensor freshness, task heartbeat/deadline checks, long-I/O deadline, prior fault latch, and every task stack margin pass.
- Do not add an unconditional watchdog refresh to make a failing board appear stable.

Continue with [Module bring-up and acceptance](BRINGUP.md).

## STM32CubeMX regeneration

Read the regeneration procedure in [STANDARDS.md](../STANDARDS.md) before opening `Atlas.ioc`. The routine baseline is STM32CubeMX 6.17.0 with STM32Cube FW_H7 1.13.0.

Generate into a disposable comparison directory first. Review clock trees, GPIO reset levels, NVIC entries, timers, UART flow control, I2C/SPI settings, middleware, user-code regions, and both build-system source lists before merging generated changes. FreeRTOS is manually integrated: do not enable CubeMX RTOS generation, overwrite the SysTick/SVC/PendSV integration, remove `ATLAS_USE_FREERTOS`, or replace the matching GNU/IAR ports.

## Troubleshooting

- **Compiler not found:** add the Arm GNU `bin` directory to `PATH` before configuring; cached compiler paths do not repair themselves.
- **Wrong compiler/generator cached:** create a fresh build directory or use `--fresh`; never edit a copied cache by hand.
- **Preset and fallback collide:** keep Ninja preset output and alternate-generator output in different directories.
- **Target links but module fails:** inspect `atlas_board.init`, the module `health` counters, and a decoded bus trace; a build is not a communication test.
- **Watchdog reset after startup:** inspect `atlas_rtos.health.fault`, its three statuses and heartbeats, `stale_sensor_mask`, application/long-I/O deadline counters, maintenance/recovery fields, stack free-word fields, `atlas_board.runtime_fault`, and driver counters before changing watchdog behavior.
- **Undefined SVC/PendSV or duplicate handler:** confirm `ATLAS_USE_FREERTOS=1`, the matching Cortex-M7 port source/assembly, handler mappings in `FreeRTOSConfig.h`, and generated-handler guards in `stm32h7xx_it.c`.
- **HAL time stops or task delays fail:** confirm the project SysTick wrapper calls `HAL_IncTick()` before `xPortSysTickHandler()` and is not replaced by a direct vector alias.
- **One toolchain fails:** compare source membership, FreeRTOS port/include selection, definitions, startup assembly, FPU mode, language extensions, and linker selection between CMake and IAR.
