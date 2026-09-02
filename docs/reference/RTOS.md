# RTOS and firmware reference

This is the runtime ownership/timing contract, not a hardware-qualification claim. Start with [Systems](../SYSTEMS.md); use [Peripheral services](../PERIPHERALS.md) for concrete commands and examples and [Development](../DEVELOPMENT.md) for contributor rules.

Unless explicitly stated, sections below describe the **normal Debug/Release** profile. Bench operation is centralized in [startup](../startup.md).

## Diagnostic profile

`ATLAS_BRINGUP=1` selects `AtlasBringup_Start()` instead of `AtlasRtos_Start()`. Board initialization prepares indicators and NOT_READY reports; `AtlasBoard_ProbeModule()` runs only in the diagnostic sensor owner, once per device per boot. The control hook/normal sensor-health supervisor are not started. `AtlasRtos_OutputsPermitted()` always returns false, and the output command boundary independently rejects configuration/PWM/pyro assertions. This is not a runtime bypass of normal flight supervision.

| Task | Priority | Stack words | Diagnostic ownership |
|---|---:|---:|---|
| AtlasOutputs | 6 | 1536 | Existing ADC/input owner; separate bounded 1 s logic-GPIO path only |
| AtlasBenchWatch | 5 | 512 | 100 ms progress/deadline/stack supervision and IWDG refresh |
| AtlasBenchUSB | 3 | 2048 | Strict parser, one pending command, escaped/bounded JSON and service-result routing |
| AtlasUSB | 2 | 1024 | Existing VBUS/CDC owner and copied transport |
| AtlasBenchIO | 1 | 2048 | Sole sensor/link/expansion/indicator owner; worker commands and latest samples |
| AtlasStorage | 1 | 2048 | Existing filesystem/RTC owner; no boot mount; explicit create-new self-test |
| Idle | 0 | 256 | Kernel idle work |

The console publishes nominally every 500 ms. Direct ADXL/LSM sampling is requested at 5 ms, MMC at 100 ms and barometer at 200 ms; slow MMC/barometer conversions are separated. Long probes pause this owner's samples but not the higher-priority console/output/USB owners. Report freshness, counts, quality and errors separately; the dashboard is not a lossless flight data recorder.

Console/owner/output progress has a 2 s supervision allowance; a declared worker command has a 40 s ceiling checked by the supervisor and on return. The supervisor checks its own, console, owner and available output stack margins. SD/USB expose stack/health separately; isolated missing media/devices do not invoke the normal required-sensor reset loop. Common assert/fatal/stack-overflow hooks still inhibit outputs. No software supervision proves hardware pulse timing or complete schedulability.

Work/results have two static entries each; console replies have four retained slots; JSON TX capacity is 8192 bytes. Only one side-effecting command is pending. Result space is reserved before dequeue, requests carry a USB connection generation, old IDs are rejected, and no uncertain operation is replayed. Disconnect does not cancel already executing filesystem/device work. Sensor publication is copied inside a bounded critical region; only the owner touches driver objects. Ordinary snapshots/command APIs are not a second client of this diagnostic scheduler.

`ATLAS_IO_BENCH_GPIO` accepts a checked, current one-second pulse on one of seven fixed logic pins, or unconditional all-low. USB/DTR/rail loss, expiry and emergency stop clear it in the output owner. The public API uses zero-based channels; the console uses 1–7, with `gpio 0` meaning all-low. It never affects PWM or pyro authorization. This task-serviced logic pulse must not drive an actuator.

## Major functions

| API / entry point | Role |
|---|---|
| `AtlasBoard_Init()` | Pre-scheduler device probes and per-stage startup report |
| `AtlasRtos_Start()` | Validate grouping, create static owners/objects and start scheduling |
| `AtlasRtos_GetSnapshot()` / `GetHealth()` | Copy coherent sensor/runtime publications; caller wait 0–5 ms |
| `AtlasRtos_SubmitCommand()` / `ReadMaintenanceReply()` | Copied indicator/link/commissioning commands and retained radio query replies |
| `AtlasRtos_ReadRadio()` / `ReadBle()` | Consume serialized raw receive streams |
| `AtlasRtos_ApplicationStep()` / `CommandCompleted()` | Weak nonblocking control/completion hooks |
| `AtlasRtos_OutputsPermitted()` / `InhibitOutputs()` | Expiring permission and permanent per-boot emergency inhibition |
| `AtlasTime_DelayMs()` / `StartCounter()` | Minimum elapsed waits and idempotent shared-timer startup |
| `AtlasStorage_*`, `AtlasUsb_*`, `AtlasIo_*`, `AtlasExpansion_*` | Separate owned peripheral services |

Per-argument data tags are in the [RTOS](../../App/Inc/atlas_rtos.h), [policy](../../App/Inc/atlas_rtos_policy.h), [board](../../App/Inc/atlas_board.h) and [time](../../App/Inc/atlas_time.h) headers.

## Layers and startup

Project-owned `App/` sits above generated HAL/pin/clock configuration. BNO085 uses the pinned CEVA SH-2/SHTP library through the Atlas I2C/reset/time adapter. FreeRTOS is manually integrated; do not generate a second kernel or CMSIS wrapper.

Before scheduling, startup attempts LED/buzzer setup, four discrete sensors, BNO085 identity/four reports, GNSS identity/RAM configuration, RFD900x transport and BLE identity. Each stage has an `atlas_board.init` result; the first failure is the aggregate. No module settings are saved automatically.

BNO085 and GNSS use `AtlasTime_StartCounter()` for TIM2. A running BUSY/CEN handle is accepted without restart or counter reset; a stopped BUSY handle is rejected. GNSS capture remains separate from base-counter ownership.

`main()` starts IWDG1 after board probes. `AtlasRtos_Start()` validates priority group 4, creates static queues/tasks and initializes the output/ADC/expansion owners before starting FreeRTOS. SD media and USB enumeration occur later in their tasks. A failed board probe is retained and later faults supervision; failure to create required resources returns to fail-stop. Early failures before IWDG starts are not covered by that later watchdog.

Logical LED colors report startup only; [physical green/blue mapping](HARDWARE.md#led-mapping-conflict) is unresolved.

## Tasks and ownership

| Task | Priority | Stack | Nominal cadence | Owns |
|---|---:|---:|---|---|
| `AtlasOutputs` | 6 | 1536 words / 6 KiB | 5 ms | ADC1/3, TIM1/3 PWM, TIM6/DMA pyro, general GPIO, output policy |
| `AtlasWatchdog` | 5 | 1024 words / 4 KiB | 100 ms | Health/freshness/deadline/stack decision; only IWDG refresher |
| `AtlasControl` | 4 | 1024 words / 4 KiB | 10 ms scheduled period | Snapshot and pure application hook; no direct bus access |
| `AtlasIO` | 3 | 2048 words / 8 KiB | Work then one tick delay | Sensors, GNSS, radio, BLE, LED/buzzer, expansion transactions |
| `AtlasUSB` | 2 | 1024 words / 4 KiB | Work then 5 ms delay | VBUS lifecycle, CDC TX/RX |
| `AtlasStorage` | 1 | 2048 words / 8 KiB | Work then 10 ms delay | FatFs, SDMMC1 polling, media generation and RTC UTC |
| Idle | 0 | 256 words / 1 KiB | When runnable | Kernel idle work |

FreeRTOS priorities increase numerically; NVIC urgency works the other way. Control outranks blocking sensor/link work. These allocations are not measured stack guarantees. The supervisor checks output/control/sensor/supervisor/idle margins against 64 free words. USB/storage publish their margins for separate monitoring; their absence or isolated service errors are not required-sensor watchdog faults.

SPI2 and I2C1 sensor operations, SPI3 IMU/external CS operations and UART mode/parsers are serialized by `AtlasIO`. A mutex around a byte transfer would not protect a whole device operation. Filesystem and USB work never execute in the control hook.

## Scheduling and data age

| Path | Requested work | Baseline freshness |
|---|---|---:|
| Board service | UART recovery, BNO085 bounded SHTP reads, GNSS parse and buzzer expiry each cycle | Errors latch |
| ADXL375 | Due every 2 ms, device ODR 400 Hz | 50 ms |
| LSM6DSV16B | Coalesced INT1 or 5 ms fallback; accel + gyro ready; device ODR 240 Hz | 50 ms |
| MMC5983MA | On-demand field due every 50 ms | 250 ms |
| MS5611 | D2/D1 OSR1024 due every 100 ms | 500 ms |
| BNO085 | Requested accel/gyro/magnetic/rotation at 100/100/50/100 Hz | 500 ms per report |
| GNSS NAV-PVT | RAM-configured 10 Hz | 1000 ms |
| PPS | TIM2 CH1 capture | Separate; not required for watchdog |
| Radio/BLE RX | Up to 64 bytes per link per cycle in data mode | Drop counters; no peer guarantee |
| ADC / output owner | Completed ADC1 scans, periodic separate VREF/temperature | 50 ms ADC / 250 ms VREF |

MMC5983MA and MS5611 slow conversions are placed in different cycles; a slow conversion, ordinary command and expansion transaction are not stacked together. Expansion reception still receives upkeep when optional transactions are deferred.

These are service requests and age limits, not lossless acquisition rates. Blocking conversion/maintenance pauses the owner's other I/O even while it yields the CPU. Interrupt coalescing/latest-value publication can skip sensor samples.

The supervisor allows 2000 ms sensor startup grace and a bounded 1200 ms recovery window after maintenance. These exceptions keep supervision alive; they **do not permit actuation or call the control hook** during incomplete startup/maintenance/recovery. GNSS fix quality and RF/BLE peer status are separate application checks.

## Snapshots, streams and result lifetime

The sensor owner increments `sequence`, stamps publication time and copies its working snapshot under a short mutex. Readers see one coherent version, not simultaneous measurements. Never retain the hook's pointer.

`valid_mask` means data has been received; check MCU timestamp, age, calibration and quality separately. BNO's SH-2 time has a different domain; use the `bno_*_received_at_ms` fields for HAL-age comparisons. NAV-PVT also needs fix/accuracy/UTC flag checks.

Each UART ISR produces its own 1024-byte ring with 1023 usable slots. The sensor owner consumes it and feeds RFD900x/BLE into separate static FreeRTOS streams with 1024 usable bytes. Reader mutexes serialize public consumers. Overflow discards new bytes with counters; flow control follows hardware, not application-ring occupancy. UART4 has its own expansion RX stream and diagnostics.

USB and storage have their own copied buffers/results; output commands use a separate eight-request/eight-result queue. Their detailed lifetime and backpressure rules are in [Peripheral services](../PERIPHERALS.md#shared-application-contract).

Raw bytes are not commands. An application protocol must define version/type/length, byte order, units, sequence, integrity, authorization, loss handling and resynchronization. A transport ACK is not a deployment or actuation acknowledgement.

## Command contract and maintenance

The main queue holds eight copied requests and executes at most one per eligible I/O cycle:

- LED color; buzzer beep/stop.
- RFD900x/BLE data: 1–64 bytes, explicit UART timeout 1–10 ms.
- Explicit radio/BLE command/data-mode transitions.
- RFD900x identity/settings query, parameter update with persistence flag, or host baud change.
- BLE SPS profile with bounded printable name and persistence flag.

A byte count may not fit a small timeout/low baud; select an adequate bounded request and chunk deliberately. CTS or link stalls can still cause partial transmission; never automatically replay an uncertain write.

Public main-queue/snapshot/health/stream waits are 0–5 ms; prefer zero in the control hook. They bound the kernel wait, not total execution latency. Submission copies the request but does not guarantee when it will run. The latest health result is not a completion log.

Radio identity/settings replies have a four-entry ticket/type/status/text queue. Capacity is reserved before executing a query; it must be drained even on failure. The reply is published **before** `AtlasRtos_CommandCompleted()` so the hook may read it immediately. Completion hooks execute in the sensor owner and must not block.

| Long operation | Fixed deadline |
|---|---:|
| RFD900x enter/exit command mode | 2200 ms |
| RFD900x identity/settings/host-baud operation | 1500 ms |
| RFD900x parameter update | 4000 ms |
| BLE enter command mode | 3500 ms |
| BLE enter data mode | 2000 ms |
| BLE SPS configuration, optional persistence/restart/readback | 35000 ms |

A maintenance operation is rejected unless pyro is disarmed, PWM disabled and general GPIO low. The check and maintenance hold are serialized against output enable. The owner publishes a busy deadline before executing the long call; supervisor and return path enforce it. The control task still advances its heartbeat but skips the application hook.

On completion, the owner publishes recovery and advances its I/O heartbeat before removing the busy gate. The output hold persists through the single 1200 ms recovery interval. Another long transition is rejected during recovery; ordinary service/sample errors still fault. Applications must explicitly re-enable/re-arm afterwards; a deadline exception never supplies output permission.

A mode transition may be a no-op when already in that mode. General arbitrary AT passthrough is not provided; use the specific reviewed commands. Ordinary boot does not persist settings. Buzzer expiry remains owner-serviced and may extend through a stall/maintenance interval; it is not a hardware cutoff.

## Time and interrupts

FreeRTOS V10.6.2 is native, preemptive/time-sliced, 1 kHz tick with FPU enabled. There is no MPU kernel wrapper, heap, timer task or tickless idle. **The complete firmware is configured for 1 kHz**; alternate-rate tests cover the delay helper, not whole-system tick-rate compatibility.

`SysTick_Handler()` advances HAL time then the kernel tick once scheduling has started. SVC/PendSV come from the matching GNU/IAR M7 r0p1 port. There is no TIM6 HAL tick; TIM6 belongs to the pyro sequencer.

`AtlasTime_DelayMs()` rounds upward and adds a tick-phase allowance for minimum elapsed time, with overflow-safe chunking. Before scheduling it uses bounded HAL-delay chunks. A zero delay is a no-op; a nonzero ISR/suspended-scheduler call asserts. These minimum waits are not maximum response-time guarantees.

Peripheral ISRs use no FreeRTOS API. TIM6 DMA completion/error, ADC flags, PVD/ECC, UART RX, PPS and EXTI callbacks perform bounded work only. A future kernel ISR API requires numerical NVIC priority 5–15 under group 4 plus a fresh latency/yield audit; high-urgency IRQs must remain kernel-independent.

## Watchdog and deadline coverage

Only the supervisor refreshes IWDG. Its 100 ms decision checks startup/service/sample status, required freshness, changed I/O/control/output heartbeats, output publication/ADC age, stacks, latched faults and refresh success. Nominal prescaler 64/reload 999 gives about two seconds at 32 kHz LSI; actual tolerance is unmeasured.

The control response deadline is measured from the **scheduled** release: dispatch latency, snapshot wait and hook all consume the 10 ms budget. Late releases skip the hook; late completion inhibits outputs and latches a fault. Missed releases resynchronize instead of running a catch-up burst. A non-maintenance I/O cycle at least 20 ms also inhibits outputs and latches its deadline fault.

The output gate additionally expires at the earliest required sensor timestamp limit. A stalled sensor owner cannot leave outputs permitted merely because the next supervisor cycle has not occurred yet. The output owner checks its own ADC and supply state each cycle.

PVD undervoltage, ECC notifications, DMA/ADC failures and fatal exceptions invoke immediate output inhibition. It is per-boot and never cleared by a later healthy sample. Fatal exception handlers do not resume the interrupted operation. Diagnostics are volatile except RTC UTC validity; no persistent crash recorder is implied.

These mechanisms detect modeled failures, but are not a schedulability proof. Measure worst-case interrupt load, control response, sensor sample loss, output-loop latency and stack/call-chain usage on the actual board. Watchdog reset is not a substitute for immediate electrical shutdown.

## Memory and extensions

Ordinary data and static task stacks remain in DTCM. Two aligned private DMA blocks occupy a 64-byte `.atlas_dma` NOLOAD section in AXI SRAM at `0x24000000`: ADC1 scan storage and the two-word pyro sequencer. Both GNU and IAR linkers explicitly place it. Startup seeds complete 64-bit ECC words before subword DMA access.

C-library heap growth is prohibited: GNU's `_sbrk()` returns ENOMEM for every request and IAR's HEAP has size zero. Radio/BLE AT lines use bounded explicit formatting; they no longer pull libc printf/allocator paths into the GNU image. A zero linker heap reservation alone did not enforce this policy in the original generated `_sbrk()`.

D-cache is off and the output owner rejects cache-on startup or changed reviewed clocks/addresses. Enabling D-cache later without a new coherency/MPU design violates this contract. SD uses CPU polling and USB uses CPU/FIFO PIO, so neither retains DMA access to DTCM callers.

The main/exception stack occupies 0x2001C000–0x2001FFFF (16 KiB), with a 256-byte no-access guard at 0x2001BF00–0x2001BFFF. GNU asserts that writable data cannot reach the guard; IAR explicitly places CSTACK and excludes both guard and stack from its general data region. This stack is separate from FreeRTOS task stacks. Physical stack/MPU/ECC validation and actual IAR compilation remain required.

For extensions, define exclusive ownership, priority/budget, static storage, copied-message bounds, overflow/cancellation, freshness, completion meaning, fault/watchdog policy and measured margins. Add sources to both builds and repeat the relevant [acceptance gates](../SYSTEMS.md#acceptance-gates).
