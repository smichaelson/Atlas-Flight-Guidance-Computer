# Quick start

For the newly assembled PCB, use **[startup](startup.md)**: it builds the isolated **Bringup** profile, programs via factory USB DFU, and runs the local dashboard. This contributor quick start describes the **normal Debug/Release application** unless stated otherwise; it is not the first-power procedure.

## What you can do today

Build the complete image, run all software checks, consume typed sensor/analog snapshots, and queue storage, USB, expansion and output work. The previously reported integration gaps now have implementations. **Physical communication, timing and electrical safety still require acceptance on the assembled board.** [Systems](SYSTEMS.md) is the authoritative readiness matrix.

The MCU is STM32H743ZIT6. `main()` configures peripherals, probes the devices, starts IWDG, and transfers to FreeRTOS. Dedicated owners handle sensors/links, outputs/ADC, USB and storage. `AtlasControl` provides a nominal 100 Hz nonblocking algorithm hook; its supplied implementation is a no-op.

## 1. Build without hardware

With CMake 3.22+, Ninja and Arm GNU binaries on `PATH`, run from the repository root:

```text
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
cmake --build --preset Release
```

Outputs belong under ignored `build/`. The review used the documented Windows Makefiles fallback; versions, commands and IAR limitations are in [Development](DEVELOPMENT.md#build).

## 2. Run the complementary checks

Use PowerShell 7 and host GCC, not the Arm compiler:

```powershell
pwsh -NoProfile -File Tests/host/run_tests.ps1
pwsh -NoProfile -File Tests/review/run_review_probes.ps1
pwsh -NoProfile -File Tests/services/run_service_tests.ps1
pwsh -NoProfile -File Tests/bringup/run_bringup_tests.ps1 -Python .\.venv\Scripts\python.exe
pwsh -NoProfile -File Tests/repository/check_repository.ps1
```

| Check | Expected result | Scope |
|---|---|---|
| Host suite | Pass | Sensor register/math, UBX/AT profiles, UART, indicators and pure RTOS policy |
| Review probes | Pass | Shared timer, minimum waits, GNSS recovery, real SD/FatFs integration on a RAM volume, analog and pyro policy |
| Service models | Pass | Output fault boundaries, real USB core/class, USB lifecycle, storage/RTC owner, expansion queues and zero-heap boundary |
| Bring-up suite | Pass | Strict console, actual C-to-Python JSON, diagnostic output denial, exclusive SD test, dashboard/fixture models and hidden UI smoke test |
| Repository checker | Pass | Local links/anchors, documentation tags, source membership and artifact policy |

A nonzero exit is a failure, not an expected red baseline. The review runner uses exit 1 for failed acceptance and 2 for harness/build errors. Service checks use 2 for compiler failures and propagate runtime failures. None of these tests accesses a physical board, USB bus, radio or card.

## 3. Understand stock startup before connecting hardware

- Sensor identity/configuration probes run before scheduling. GNSS and BNO085 share an idempotently started TIM2; neither resets the other's timebase.
- ADC/voltage/input monitoring runs without output qualification. Invalid readings remain marked invalid.
- Storage makes a read-only boot mount attempt. Absence or an invalid filesystem is nonfatal to unrelated services. No boot log, formatting or deletion occurs.
- USB exposes its pull-up only after stable VBUS; transmission requires enumeration. It is a bounded byte transport, **not a supplied console or command interpreter**.
- RFD900x starts as a transparent UART transport; this alone does not identify the modem or prove a peer link. BLE is identity-probed and left in command mode. Commissioning is explicit.
- PWM is disabled with signal pins low; general outputs are low; pyro is disarmed. No configuration defaults authorize actuation.

## 4. Inspect a board only under an approved inert test plan

Keep J5 open. Disconnect igniters, motors, servos and other energetic loads; use current-limited power and SWD. Do not format media, transmit RF, persist settings or raise gates as incidental smoke tests.

1. Confirm the assembled revision, polarity, rail limits and wiring using the [hardware reference](reference/HARDWARE.md). The pyro feed bypasses the main eFuse.
2. Break after `AtlasBoard_Init()`, before IWDG startup, and inspect each `atlas_board.init` result. Preserve first-failure evidence.
3. Under scheduling, inspect `AtlasRtos_GetHealth()`, sensor timestamps and `AtlasIo_GetSnapshot()`. Required sensor/ADC/service faults inhibit outputs and withhold watchdog refresh; do not bypass this to keep a broken setup running.
4. Inspect optional USB/storage health independently. Their absence is not a flight-control watchdog fault.
5. Treat the logical LED state as a startup hint only. Its green/blue schematic mapping remains unresolved.

No firmware was flashed during the correction review. Hardware acceptance is a separate, explicit activity.

## 5. Add application behavior in one place

Start with [the non-actuating integration example](DEVELOPMENT.md#application-integration). Put project logic in `App/` and add its source to GNU and IAR builds. Read snapshots, check validity/age/quality, and use copied-message APIs; do not call HAL or a device driver from an application task.

[Peripheral services](PERIPHERALS.md) gives the complete queue/result contracts and short examples for SD, USB, ADC, GPIO, PWM, pyro, expansion and radio/BLE commissioning. Read the relevant section when you need it; ten optional module references contain protocol detail.
