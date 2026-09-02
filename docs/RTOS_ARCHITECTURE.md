# Atlas RTOS Architecture and Application Contract

## Purpose

Atlas runs FreeRTOS so estimation, guidance, control, telemetry, and logging can be added without re-solving device ownership or allowing concurrent code to corrupt a bus transaction. This document is the authoritative runtime contract for tasks, priorities, sampling, inter-task data, interrupts, and watchdog behavior.

The implementation is target-built and policy-tested. It has not yet been timed or exercised on an assembled board, so measured execution time, task stack minima, sensor freshness, and hardware communication remain bench-acceptance requirements.

## Runtime at a glance

```text
hardware IRQs                         control / estimation hook
    |                                          |
    | flags, UART bytes, PPS only              | snapshots + bounded commands
    v                                          v
+----------------+       coherent       +----------------------+
| Atlas I/O task | ---- snapshots ----> | Atlas application task|
| sole driver    | <--- command queue --| 100 Hz weak hook      |
| owner          |                      +----------------------+
+-------+--------+
        | health + heartbeat                  health + heartbeat
        +-------------------+---------------------------+
                            v
                 +------------------------+
                 | Watchdog supervisor    |
                 | highest task priority  |
                 | sole IWDG refresher    |
                 +------------------------+
```

## Kernel selection and configuration

| Item | Atlas setting |
|---|---|
| Kernel | FreeRTOS Kernel V10.6.2 from STM32CubeH7 1.13.0 |
| API | Native FreeRTOS API; no CMSIS-RTOS wrapper |
| Target port | Cortex-M7 r0p1, GNU and IAR variants |
| Scheduling | Preemptive, time slicing enabled, 1 kHz tick |
| Allocation | Static only; `configSUPPORT_DYNAMIC_ALLOCATION=0`; no `heap_*.c` |
| FPU / MPU | FPU enabled; FreeRTOS MPU wrapper disabled |
| Software timers | Disabled; Atlas does not create the timer-service task |
| Tickless idle | Disabled until timing and low-power behavior are measured |
| Diagnostics | Level-2 stack-overflow checking, `configASSERT`, live high-water marks |

Project settings are in [`FreeRTOSConfig.h`](../App/Inc/FreeRTOSConfig.h); project-owned integration is in [`atlas_rtos.h`](../App/Inc/atlas_rtos.h) and [`atlas_rtos.c`](../App/Src/atlas_rtos.c). The copied kernel subset and provenance are under `ThirdParty/FreeRTOS-Kernel/`.

## Startup and ownership transfer

`main()` initializes clocks and generated peripherals, then calls `AtlasBoard_Init()` before scheduling begins. This preserves the existing complete startup report and keeps multi-second identity/configuration probes outside watchdog supervision. After all probes have been attempted:

1. `main()` starts IWDG1.
2. `AtlasRtos_Start()` requires `NVIC_PRIORITYGROUP_4`, creates every queue, mutex, stream, task, TCB, and stack from static storage, and starts the scheduler.
3. The I/O task becomes the exclusive owner of `AtlasBoard` and every driver API.
4. The application task receives copies only; it must never dereference `atlas_board` or call a device driver.
5. The supervisor becomes the only code permitted to refresh IWDG1.

A healthy `AtlasRtos_Start()` never returns. If allocation/object creation fails or the scheduler returns, `main()` enters the fail-stop path and the independent watchdog resets the MCU.

## Task set

FreeRTOS priorities increase numerically; the supervisor is deliberately highest.

| Task | Priority | Static stack | Nominal cadence | Responsibility |
|---|---:|---:|---:|---|
| `AtlasWatchdog` | 5 | 512 words / 2 KiB | 100 ms | Check task progress, sensor freshness, status/deadline latches, and stack margins; refresh IWDG only on complete success. |
| `AtlasIO` | 4 | 2,048 words / 8 KiB | 1 ms between cycles | Sole post-start driver owner; service protocols, sample sensors, execute one command, drain RX streams, publish a snapshot. |
| `AtlasControl` | 3 | 1,024 words / 4 KiB | 10 ms / 100 Hz | Copy the latest snapshot, call the weak application hook, then publish a heartbeat only after the hook returns. |
| FreeRTOS idle | 0 | 256 words / 1 KiB | as available | Kernel idle work; no Atlas hardware ownership; high-water margin is supervised. |

These sizes are conservative initial allocations, not measured worst-case requirements. The supervisor records the minimum unused stack words reported by FreeRTOS and latches a fault below 64 words. Bench testing must capture high-water marks under worst-case protocol traffic, faults, floating-point control work, and compiler optimization before stacks are reduced.

## I/O task and sensor schedule

All complete SPI and I²C transactions are serialized by ownership instead of per-bus mutexes. This is stronger than protecting individual calls: no second task can split a device transaction, select another SPI target, consume a parser response, or alter a module mode mid-operation.

| Path | Runtime action | Nominal rate / bound |
|---|---|---|
| `AtlasBoard_Service()` | BNO085/SHTP, GNSS UBX, UART recovery, buzzer expiry | Every I/O cycle; BNO work remains capped at eight reads per call |
| ADXL375 | Poll DATA_READY, then coherent XYZ burst | 2 ms poll, matching 400 Hz sensor configuration without reading stale data |
| LSM6DSV16B | React to coalesced INT1 or fallback poll, require accel+gyro ready, then burst | Interrupt-driven with a 5 ms fallback |
| MMC5983MA | Automatic SET/RESET on-demand field conversion | 50 ms / 20 Hz, 10 ms completion bound |
| MS5611 | D2 then D1 at OSR 1024 and compensate | 100 ms / 10 Hz; two rounded-up 3 ms conversion waits |
| BNO085 | Consume latest accel, calibrated gyro, calibrated field, and rotation vector reports | Configured at 100/100/50/100 Hz; actual arrival is interrupt/protocol driven |
| GNSS | Consume newest NAV-PVT and coherently copy PPS | NAV-PVT configured at 10 Hz; PPS interrupt driven |
| RFD900x / BLE RX | Drain up to one UART chunk per cycle into separate RTOS streams | 1,024 usable bytes per stream; new overflow is counted, never hidden |

Successful readiness polls with no new direct-sensor sample are not faults. Transport, identity, protocol, conversion, or sample errors latch `sampling_status` or `service_status`, latch `board.runtime_fault`, and make future watchdog decisions fail. Snapshot validity is independent of status: callers must check the relevant `ATLAS_RTOS_VALID_*` bit before using a field.

After a two-second startup grace, watchdog supervision also requires fresh ADXL375 and LSM6DSV16B samples (at most 50 ms old), MMC5983MA (250 ms), MS5611 (500 ms), every configured BNO085 report (500 ms), and GNSS NAV-PVT (1,000 ms). Missing validity is stale. GNSS PPS is intentionally not required because antenna/sky conditions are environmental; PPS validity is still published for application-level navigation checks. BLE peer connectivity and RFD peer delivery are likewise not watchdog prerequisites.

The nominal one-millisecond loop is not a promise of one-millisecond sensor latency. Conversion waits yield the I/O task, and several due operations can share a cycle. `io_deadline_misses` counts cycles longer than 20 ms for characterization; measured rate/jitter requirements are still to be established.

## Coherent snapshots

`AtlasRtosSnapshot` is an immutable publication from the application’s perspective. The I/O task updates a private working copy, increments `sequence`, stamps `published_at_ms`, and copies the complete structure under a short mutex. `AtlasRtos_GetSnapshot()` copies it under the same mutex, so fields never come from two publications.

Important interpretation rules:

- `valid_mask` means at least one valid sample of that type has been published since scheduler start. It does not, by itself, mean the sample is recent.
- Each driver’s own timestamp remains the measurement/acquisition timestamp. `published_at_ms` is when the aggregate copy was published.
- A snapshot can legitimately contain sensors of different ages because their rates differ.
- The watchdog applies the baseline limits above; control/estimation code must still apply any tighter, phase-specific age and quality limits before using a value.
- GNSS validity here means a checksum-valid NAV-PVT frame was decoded, not that `fix_type`, flags, or accuracy meet a navigation requirement.
- Radio/BLE mode fields describe the local driver/module mode, not peer connectivity or delivery.
- `maintenance_active` requires the application to inhibit control that depends on fresh samples. `sensor_recovery_active` remains true for the bounded backlog-recovery window following that maintenance operation.

The default application hook is intentionally harmless:

```c
void AtlasRtos_ApplicationStep(const AtlasRtosSnapshot *snapshot,
                               uint32_t now_ms)
{
    if ((snapshot->valid_mask & ATLAS_RTOS_VALID_LSM6DSV16B) == 0U)
    {
        return;
    }

    /* Reject stale data before passing typed values into estimation/control. */
    if ((uint32_t)(now_ms - snapshot->lsm6dsv16b.timestamp_ms) > 20U)
    {
        return;
    }

    /* Estimation/control work belongs here; never call AtlasLsm6dsv16b_*(). */
}
```

Override the weak hook in exactly one project-owned application source file. It must return before its next required deadline, must not wait indefinitely, and must not call `AtlasBoard_*`, a driver, HAL bus I/O, or an RTOS `FromISR` API.

The complete snapshot-copy-plus-hook cycle is timed with the wrap-safe HAL tick. A duration of 10 ms or more latches `APPLICATION_DEADLINE`, increments `application_deadline_misses`, and permanently stops watchdog refresh for that boot. This millisecond-resolution guard is deliberately conservative; bench timing should use finer instrumentation before flight requirements are approved.

## Commands and output ownership

`AtlasRtos_SubmitCommand()` copies one validated request into an eight-entry static queue. The I/O task executes at most one command per cycle and reports completion through `AtlasRtos_CommandCompleted()` plus the latest ticket/type/status in `AtlasRtosHealth`.

Supported work:

- set one LED color;
- start a validated, bounded buzzer beep or stop the buzzer;
- write up to 64 transparent RFD900x or BLE bytes with a 1–50 ms UART timeout;
- enter/exit guarded RFD900x command mode;
- enter NINA-B112 command or data mode through the verified driver transitions.

Example:

```c
AtlasRtosCommandRequest command = {
    .type = ATLAS_RTOS_COMMAND_BUZZER_BEEP,
    .arguments.buzzer = {
        .frequency_hz = ATLAS_BUZZER_RESONANT_FREQUENCY_HZ,
        .duration_ms = 100U
    }
};
uint32_t ticket;

/* Zero wait is preferred inside the 100 Hz control hook. */
AtlasStatus queued = AtlasRtos_SubmitCommand(&command, 0U, &ticket);
```

Queue acceptance is not hardware success. Use the completion hook or health result, and design application behavior for `ATLAS_ERROR_BUSY`, mode errors, timeout, absent peer, and link loss. The completion hook runs inside the I/O task and therefore must be short, nonblocking, and driver-free.

Long BLE/RFD mode changes are maintenance operations, not in-flight control operations. Before the driver call begins, the I/O task publishes `maintenance_active=true`; the application must use that as a control inhibit. While the yielding call remains within its fixed deadline, the supervisor may accept an unchanged I/O heartbeat and temporarily defer freshness, but the application heartbeat must still advance. Deadline expiry is checked both by the supervisor and by the I/O task when the call returns, so a transition cannot overrun between supervisor ticks unnoticed.

On return, the I/O task clears `maintenance_active` and opens one 1,200 ms `sensor_recovery_active` window so accumulated GNSS/SHTP traffic can be drained and every required timestamp can advance. It publishes that recovery state and advances the I/O heartbeat before atomically removing the busy gate, so the supervisor cannot observe a false stall or stale pre-maintenance snapshot at the handoff. I/O liveness remains mandatory during recovery, ordinary service/sample errors still fail immediately, and stale data faults when the recovery deadline expires. A second long transition is rejected with `ATLAS_ERROR_BUSY` during this window, preventing indefinite freshness deferral. Short LED, buzzer, and bounded payload commands remain available. Any command dequeued after a startup, service, sample, or RTOS fault completes with `ATLAS_ERROR_STATE` without touching hardware.

Configuration functions that write NVM or require arbitrary response buffers—BLE SPS commissioning and RFD S-register changes—remain deliberate pre-scheduler/maintenance operations. Do not call them from `AtlasRtos_ApplicationStep()` or the completion hook.

## Transparent receive streams

The I/O task is the only consumer of each interrupt-owned UART ring. It forwards ordinary transparent RFD900x and BLE payload to separate static FreeRTOS stream buffers. `AtlasRtos_ReadRadio()` and `AtlasRtos_ReadBle()` serialize callers and return raw bytes; they do not impose framing.

Each stream has 1,024 usable bytes. If it fills, already buffered bytes stay ordered, newly drained bytes are discarded, and the matching `*_rx_dropped_bytes` counter increases. Any drop must invalidate the future message layer until its length/integrity/sequence scheme resynchronizes.

BLE RX/TX is available only in Data mode. Ordinary startup intentionally leaves the identified module in Command mode; request the verified Data-mode transition when the commissioned module and peer state permit it. The RFD transport starts in transparent mode but its local UART rate still has to match the external modem.

## Delay behavior

All project driver waits go through `AtlasTime_DelayMs()`:

- before scheduling, it uses `HAL_Delay()` so startup probes remain usable;
- after scheduling, it uses `vTaskDelay()` and yields the calling task;
- in interrupt context, it asserts because a blocking delay would be invalid.

Direct `HAL_Delay()` calls are prohibited in project runtime drivers. HAL’s millisecond timebase and the FreeRTOS tick both use the existing SysTick at 1 kHz: `SysTick_Handler()` advances HAL first, then the kernel once scheduling has started.

## Interrupt contract

Atlas uses `NVIC_PRIORITYGROUP_4`: all four implemented bits are preemption priority and there are no subpriority bits.

The kernel uses priority 15 for SysTick/PendSV and sets `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` to 5. Current peripheral callbacks call no FreeRTOS API, including no `FromISR` function. Existing high-urgency priorities 0–4 therefore remain legal.

If a future ISR calls any FreeRTOS `...FromISR()` API:

1. its numerical NVIC priority must be 5 through 15;
2. its priority must be explicitly reviewed against latency requirements;
3. it must use only the documented ISR-safe API and the `higherPriorityTaskWoken`/yield pattern;
4. all remaining work must stay deferred to a task;
5. the interrupt-priority and fault-injection tests must be repeated.

Do not map the FreeRTOS tick handler directly onto the vector. The project-owned SysTick wrapper is required so the STM32 HAL tick continues to advance. SVC and PendSV are supplied directly by the FreeRTOS port through the names mapped in `FreeRTOSConfig.h`.

## Watchdog and fault policy

Only `AtlasWatchdog` calls `HAL_IWDG_Refresh()`. Every 100 ms it requires all of the following:

1. aggregate startup status is `ATLAS_OK`;
2. board service status has never failed;
3. sensor sampling status has never failed;
4. every required sensor is valid and inside its baseline age limit after startup/recovery grace;
5. the I/O heartbeat changed, or the I/O task is inside a declared long operation whose deadline is still in the future;
6. the application heartbeat changed only after a completed hook call;
7. the snapshot-copy-plus-hook cycle has not consumed its 10 ms period;
8. I/O, application, supervisor, and idle stacks each retain at least 64 unused words;
9. no prior supervisor fault has latched;
10. the HAL watchdog refresh itself succeeds.

The first fault is permanent until reset. Recovery in a later cycle does not resume feeding. `AtlasRtosHealth.fault` names the reason; `stale_sensor_mask` latches the required sensor bits responsible for `SENSOR_STALE`, while driver/board health retains lower-level causes. Assertions and stack-overflow hooks disable interrupts before recording available context, preventing a final watchdog refresh during partial fault capture, and then rely on IWDG for reset.

The current firmware does not retain diagnostics through reset. Adding a reset-cause/crash record is still required before operational use.

## Memory and build integration

Every task, queue, mutex, and stream has application-supplied static storage. The linked image contains no `pvPortMalloc` symbol and no FreeRTOS heap source. Current Arm GNU evidence is recorded in [Building](BUILDING.md) and [Validation](VALIDATION.md).

Both build systems must remain equivalent:

- CMake compiles kernel common sources plus `portable/GCC/ARM_CM7/r0p1/port.c`.
- IAR compiles the same common sources plus the IAR r0p1 `port.c` and `portasm.s`.
- Both define `ATLAS_USE_FREERTOS=1` and include the common kernel headers plus the matching port directory.

`Atlas.ioc` does not own this manually integrated middleware. Enabling CubeMX FreeRTOS generation would create a second kernel/wrapper and is prohibited. Generate to a comparison directory and preserve the RTOS source lists, exception integration, SysTick wrapper, compile definition, and user-code changes explicitly.

## Adding a task or service

Before adding a task:

1. justify why the work cannot run in the existing application hook or I/O owner;
2. define ownership of every object it reads or writes;
3. assign priority from measured latency/deadline needs, not perceived importance;
4. allocate a static stack and object control block;
5. bound every wait and prove it cannot block a required heartbeat;
6. add its progress and stack margin to watchdog supervision if it is required for safe operation;
7. do not add direct driver access—extend snapshots/commands instead;
8. test queue-full, timeout, task-stall, stack-low, tick-wrap, and device-fault behavior;
9. review Debug/Release maps and both build-system memberships;
10. update this document, project status, validation, and bring-up evidence.

## RTOS bench acceptance

On an inert, current-limited board, retain evidence for all of the following before calling runtime integration bench-verified:

1. SysTick and HAL time remain 1 kHz after scheduler start; PPS timing remains correct.
2. All ten snapshot validity bits expected from connected devices become set and sample timestamps advance at measured rates.
3. Sensor/update latency and I/O-cycle jitter remain inside documented requirements under simultaneous GNSS, BNO085, radio, and BLE traffic, including the 2 s startup and 1.2 s maintenance-recovery boundaries.
4. Radio/BLE payload stress causes no unexplained UART or RTOS stream drops.
5. Every command type completes with the expected ticket/status and never permits direct concurrent driver access.
6. Measured stack high-water marks retain an approved margin in Debug and Release under worst-case traffic and fault paths.
7. Deliberately stall and overrun the application hook, stall I/O, exceed a long-operation deadline, withhold each required sensor stream, force a sample/service error, and simulate low stack margin; each must latch the correct fault and stop watchdog refresh.
8. Confirm the watchdog resets the MCU within the measured IWDG interval and every hazardous output remains benign throughout reset/reboot.
9. Repeat tick-counter rollover policy tests and a long-duration run sufficient to expose queue/stream accumulation.

Source review and emulation cannot replace these measurements.

## Primary references

- FreeRTOS, [Static versus dynamic memory allocation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation).
- FreeRTOS, [Cortex-M interrupt priority and ISR API rules](https://freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4).
- FreeRTOS Kernel, [Cortex-M7 r0p1 port notes](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/portable/GCC/ARM_CM7/ReadMe.txt).
- STMicroelectronics, [CMSIS-RTOS API description for STM32Cube](https://www.st.com/resource/en/user_manual/dm00105262-cmsis-rtos-api-description-stmicroelectronics-stm32cube-mcu-package-stmicroelectronics.pdf). Atlas uses native FreeRTOS APIs; this ST document is retained for STM32 integration context.
