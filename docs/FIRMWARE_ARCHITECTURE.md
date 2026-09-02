# Firmware Architecture

## Purpose

Atlas separates register/protocol work from future estimation, guidance, control, logging, and telemetry. The runtime is a statically allocated FreeRTOS system: control code receives typed, coherent publications and requests output/module actions without owning any hardware driver.

The detailed scheduling, priority, interrupt, watchdog, and application API contract is in [RTOS Architecture](RTOS_ARCHITECTURE.md). This page explains the complete layering and system failure model.

## Layering

```text
future estimation / guidance / control / telemetry / logging
                             |
            AtlasRtos snapshots, commands, RX streams
                             |
         +-------------------+-------------------+
         | static FreeRTOS tasks and supervisor |
         +-------------------+-------------------+
                             |
                         AtlasBoard
             +---------------+---------------+
             |               |               |
        typed sensors   protocol drivers   indicators
             |               |               |
       bounded SPI/I2C   interrupt UART   GPIO / PWM
                             |
             STM32Cube-generated HAL, pins, clocks
                             |
               Atlas Flight Computer Rev. 0.1
```

### Generated layer

`Core/`, `FATFS/`, `USB_DEVICE/`, and `cmake/stm32cubemx/` remain generator-managed. `main.c` initializes peripherals, constructs `AtlasBoardHardware`, completes bounded board probes, starts IWDG, and transfers control to `AtlasRtos_Start()`. The SysTick user section advances the HAL tick before the FreeRTOS tick; generated SVC/PendSV bodies are omitted when the RTOS build definition is active because the kernel port supplies them.

The RTOS middleware is intentionally project-integrated rather than enabled in `Atlas.ioc`. CubeMX regeneration must preserve those user sections and must not add a second FreeRTOS/CMSIS-RTOS layer.

### Project-owned layer

`App/Inc` and `App/Src` contain stable driver APIs, the board composition, RTOS integration, watchdog policy, and scheduler-aware delay abstraction. Public symbols are module-prefixed, hardware quantities carry units, waits are bounded, and all persistent module changes are explicit.

No project code allocates from a heap. Driver objects, receive rings, task stacks, RTOS object control blocks, queues, and stream buffers all have fixed storage.

### Third-party layer

- The BNO085 uses CEVA’s official Apache-2.0 SH-2/SHTP implementation under `ThirdParty/CEVA/sh2`; Atlas supplies the STM32 transport/reset/time adapter.
- Scheduling uses the MIT-licensed FreeRTOS Kernel V10.6.2 subset under `ThirdParty/FreeRTOS-Kernel`, copied from the exact installed STM32CubeH7 1.13.0 package. GNU and IAR use their matching Cortex-M7 r0p1 ports.

See [third-party provenance](THIRD_PARTY.md).

## Startup contract

`AtlasBoard_Init()` runs before the scheduler and attempts every subsystem even after an earlier failure. This produces a complete `AtlasBoardInitReport` instead of hiding later hardware behind the first fault.

The intentional sequence is:

1. Force LED/buzzer safe and show blue during bring-up.
2. Identify/configure/read back ADXL375, LSM6DSV16B, MMC5983MA, and MS5611.
3. Reset/identify BNO085 and enable four volatile reports.
4. Identify GNSS, apply a RAM-only 10 Hz UBX configuration, and require exact key readback.
5. Start RFD900x transport without an escape or modem command.
6. Reset/identify BLE in normal boot mode without saving settings.
7. Show green only if every startup step passed; otherwise show yellow.
8. Start IWDG, create static RTOS objects/tasks, and start scheduling.

If startup status is not `ATLAS_OK`, the tasks still begin long enough to preserve debugger-visible diagnostics, but the supervisor latches a startup fault and does not refresh IWDG.

## Runtime ownership

After `AtlasRtos_Start()`:

- `AtlasIO` is the only task permitted to call `AtlasBoard_Service()` or any sensor/module/output driver.
- Hardware callbacks only capture PPS, update interrupt flags/counters, or feed the allocation-free UART rings.
- `AtlasControl` receives a complete `AtlasRtosSnapshot`; it cannot mutate driver state.
- Output, radio, and BLE operations cross an eight-entry command queue and are executed by `AtlasIO`.
- Transparent radio/BLE receive data crosses separate 1,024-byte RTOS stream buffers.
- `AtlasWatchdog` is the only IWDG refresher.

This single-owner design serializes both SPI2 devices and both I2C1 devices without a bus mutex and also protects higher-level state: no task can steal an AT/UBX/SHTP response, change command/data mode during payload handling, or interleave a chip-select transaction.

## Data publication

The I/O task maintains a private working snapshot. After service/sampling/commands it records module modes, increments a sequence number, timestamps the publication, and copies the entire structure under a short mutex. Application readers therefore see one complete version.

The snapshot contains:

- direct ADXL375, LSM6DSV16B, MMC5983MA, and MS5611 samples;
- latest BNO085 accelerometer, calibrated gyroscope, calibrated magnetic field, and rotation vector reports;
- latest GNSS NAV-PVT and coherent PPS state;
- individual direct-sensor statuses, board-service status, module modes, maintenance/recovery state, a validity mask, and publication time.

Validity is not freshness. The supervisor enforces conservative baseline ages for all required sensor streams after startup/recovery grace, while algorithms must apply any tighter phase-specific limit and interpret GNSS fix/accuracy flags before use.

## UART data paths

GNSS, RFD900x, and BLE each own one `AtlasUartTransport`. The receive-to-idle callback is the only ring producer; the I/O task is the only ring consumer. New bytes are dropped and counted when a ring or RTOS receive stream is full; unread bytes are never overwritten silently.

GNSS bytes are parsed in the I/O task and published as typed navigation data. RFD900x and BLE payload remain opaque streams because their versioning, message type, length, byte order, sequence, integrity, authorization, and retry policy have not yet been selected.

BLE command/data transitions occur only through verified driver operations. RFD900x escape entry enforces the reviewed guard interval. Longer mode changes are maintenance-only: they publish a control-inhibit flag, declare a supervisor-visible deadline, and yield through `AtlasTime_DelayMs()` so the application and supervisor can still run. Their return path checks the deadline independently and starts one non-renewable 1.2-second sensor-backlog recovery window.

## Interrupt and timebase contract

`NVIC_PRIORITYGROUP_4` assigns all four implemented bits to preemption priority. Current peripheral ISRs call no FreeRTOS API; SysTick alone enters the kernel through the reviewed tick wrapper. This allows BNO/LSM/PPS interrupts at numerical priorities below the kernel’s maximum syscall priority without violating the Cortex-M port rule.

SysTick remains the shared 1 kHz timebase. `SysTick_Handler()` calls `HAL_IncTick()` unconditionally and calls `xPortSysTickHandler()` only after scheduling starts. Driver waits call `AtlasTime_DelayMs()`: HAL timing before scheduler start, task blocking after it, and an assertion if misused in an ISR.

## Failure model

| Failure | Runtime behavior |
|---|---|
| Wrong identity or configuration readback | Startup entry fails; data is never marked valid; startup fault prevents watchdog refresh. |
| SPI/I2C timeout or sample failure | Typed error and module counter; sampling status and `board.runtime_fault` latch; watchdog refresh stops. |
| UART hardware error | ISR requests recovery; I/O service aborts/rearms; unrecoverable service failure latches. |
| UART/RTOS stream overflow | New bytes are dropped and counted; message layer must resynchronize. |
| Invalid GNSS checksum | Frame discarded and counted; parser returns to sync search. |
| BLE/radio mode or payload command failure | Completion reports failure; it is visible but does not by itself claim the whole board has failed. |
| BNO085 asynchronous reset | Service returns a state fault because report configuration was lost; runtime latch stops watchdog. |
| Required sensor absent/stale | After startup/recovery grace, supervisor latches `SENSOR_STALE` and the offending validity bits. |
| I/O or application task stall | Heartbeat does not change within the 100 ms supervisor interval; a permanent fault latches. |
| Application snapshot/hook cycle reaches 10 ms | Application task latches `APPLICATION_DEADLINE`; queued hardware work is rejected after the fault. |
| Declared long I/O exceeds deadline | Supervisor or the independent return-path check latches `IO_DEADLINE`; later completion cannot resume refresh. |
| Task stack headroom below 64 words | Supervisor latches `STACK_MARGIN`; stack-overflow hook has a separate terminal fault. |
| Assertion / scheduler failure | Diagnostic fields are set where possible; interrupts stop and IWDG resets the MCU. |

The fault latch is monotonic until reset. The supervisor cannot resume refreshing after a later healthy cycle.

No driver energizes event outputs, actuator headers, or other hazardous loads.

## Extending the system

Before adding flight algorithms:

1. Complete module and RTOS bench acceptance, including measured sample rates/jitter and stack high-water marks.
2. Establish revision-controlled body-frame transforms and calibration provenance.
3. Define phase-specific stale-data limits (at least as strict as the supervisor baseline), plausibility, and cross-sensor disagreement policy.
4. Override `AtlasRtos_ApplicationStep()` in one application source; do not call a driver.
5. Extend snapshots or commands when new hardware interaction is needed.
6. Define versioned logging/telemetry framing with units, byte order, integrity, authorization, and reset behavior.
7. Add any required task to watchdog supervision with a static stack, explicit ownership, bounded waits, and measured deadline.
8. Re-run host tests, clean Debug/Release builds, map/stack review, IAR membership inspection, all three RTOS reviews, and affected hardware tests.
