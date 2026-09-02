# Development and engineering standards

This is the canonical contributor guide: build/test commands, the application entry point, and engineering rules. Current capability lives in [Systems](SYSTEMS.md), not in build success. Detailed task contracts live in [RTOS](reference/RTOS.md).

For physical PCB bring-up, use **[startup](startup.md)**. `Bringup` and `BringupRelease` set `ATLAS_BRINGUP=1`, produce `Atlas-Bringup.elf/.hex/.bin/.manifest.json`, and select the inhibited diagnostic scheduler. Normal `Debug`/`Release` explicitly set it to zero and produce `Atlas.*`. Both profiles must build after changes to shared drivers/services. The VS Code default build task selects Bringup; no task flashes automatically.

## Build

The reviewed environment used Arm GNU 14.3.1 (Arm build 14.174), CMake 3.26.4, Windows MinGW Makefiles/GNU Make, host GCC 13.1.0, and PowerShell 7. CMake's project minimum is 3.22. The repository presets select Ninja; the latest review built with the Makefiles fallback, not Ninja. IAR XML/source membership was checked, but no IAR compiler was available.

Put `arm-none-eabi-gcc`, `arm-none-eabi-g++`, `arm-none-eabi-objcopy` and `arm-none-eabi-size` on `PATH`. Run from the repository root:

```text
cmake --preset Debug
cmake --build --preset Debug --clean-first
cmake --preset Release
cmake --build --preset Release --clean-first
```

On Windows, if using the reviewed generator fallback, keep separate build directories. Quote each complete `-D` argument in PowerShell:

```powershell
cmake -S . -B build/ArmDebug -G "MinGW Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build/ArmDebug --parallel 4
cmake -S . -B build/ArmRelease -G "MinGW Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake" "-DCMAKE_BUILD_TYPE=Release"
cmake --build build/ArmRelease --parallel 4
```

If CMake cannot find Make, also supply `"-DCMAKE_MAKE_PROGRAM=<absolute-path-to-gmake.exe>"` with your installed path. Use a new build directory after changing generator/compiler; do not copy or hand-edit another machine's cache. Generated artifacts belong under ignored `build/` or outside the repository.

The checked-in editor settings expect ST's `cube-cmake`/`cube` tools and a specific bundled compiler location. Their presence is not required for the command-line workflow above, and that editor integration was not executed in this review. If editor diagnostics disagree with a build, select the actual build's `compile_commands.json` and toolchain; do not change target definitions to satisfy a host/MSVC IntelliSense fallback.

For IAR, open [Project.eww](../EWARM/Project.eww), confirm the Atlas project and active [flash linker configuration](../EWARM/stm32h743xx_flash.icf), and rebuild. The project uses the IAR Cortex-M7 r0p1 port plus `portasm.s`, not the GNU port. Actual IAR build and bench evidence remain required before claiming parity.

Retain compiler output, ELF/map hashes, memory sizes, selected port/handler symbols, and stack-usage output for each functional change. The [review report](REVIEW_REPORT.md#executable-evidence) owns the latest measured sizes. Static stack frames are not whole-call-chain or interrupt worst-case bounds.

## Tests

```powershell
pwsh -NoProfile -File Tests/host/run_tests.ps1
pwsh -NoProfile -File Tests/repository/check_repository.ps1
pwsh -NoProfile -File Tests/review/run_review_probes.ps1
pwsh -NoProfile -File Tests/services/run_service_tests.ps1
pwsh -NoProfile -File Tests/bringup/run_bringup_tests.ps1 -Python .\.venv\Scripts\python.exe
```

- **Host suite:** selected sensor register/math, UBX, AT/profile, LED/buzzer, UART overflow and watchdog-policy contracts. It does not run the complete FreeRTOS kernel, board startup, BNO085/SHTP, SD or USB on hardware.
- **Repository checker:** local Markdown targets, required guides, App file/function documentation, excluded artifacts, static-allocation policy and CMake/IAR membership. It does not certify runtime behavior or external URLs.
- **Review probes:** corrected timer/delay/GNSS, analog/pyro policy and real SD/BSP/disk/FatFs integration on a RAM volume. They must pass; exit 1 means a behavioral failure and 2 a harness/build error.
- **Service models:** real output-owner boundaries, actual USB core/class control and data routing, scripted USB/RTC/storage task lifecycles and expansion transactions. The finite task model is not FreeRTOS scheduling or silicon. See [evidence and limits](REVIEW_REPORT.md#executable-evidence).

Host executables are generated in the OS temporary directory. Use host GCC for these tests, not `arm-none-eabi-gcc`. Do not run the existing host-suite script concurrently with itself: its output filename is shared.

The **bring-up suite** adds strict command/framing/fuzz tests, production console routing/backpressure and actual C-to-Python JSON compatibility, diagnostic GPIO/output denial, exclusive SD file comparison/fault cases, desktop session/fixture/image models, actual UI lifecycle tests against an inert serial substitute, and a hidden Tk smoke test. Python/Tk is required; pyserial and hardware are not required for these tests. Temporary fixture files are retained in a unique directory. These tests are not proof of real COM/Windows-driver interoperability.

## Application integration

Override `AtlasRtos_ApplicationStep()` in one project-owned source and add it to both [CMake](../CMakeLists.txt) and [IAR](../EWARM/Atlas.ewp). The example below is a non-actuating integration pattern, not supplied flight logic:

This hook belongs to the **normal application**. The diagnostic profile never calls it, even if an override is linked. Keep board testing out of flight-control hooks and preserve the compile-time output gate.

```c
/**
 * @file atlas_application.c
 * @brief Non-actuating example of consuming an Atlas RTOS snapshot.
 * Major functions: AtlasRtos_ApplicationStep() validates a barometer input.
 */
#include "atlas_rtos.h"

/**
 * @brief Inspect a fresh barometer input without taking hardware ownership.
 * @param snapshot Read-only publication, valid only during this call.
 * @param now_ms Current MCU monotonic time in milliseconds.
 * @note This example neither commands an output nor establishes flight readiness.
 */
void AtlasRtos_ApplicationStep(const AtlasRtosSnapshot *snapshot, uint32_t now_ms)
{
    AtlasRtosHealth health;
    if ((snapshot == NULL) ||
        !AtlasRtos_GetHealth(&health, 0U) ||
        (health.state != ATLAS_RTOS_STATE_RUNNING) ||
        (health.fault != ATLAS_RTOS_FAULT_NONE) ||
        (health.startup_status != ATLAS_OK) ||
        (health.service_status != ATLAS_OK) ||
        (health.sampling_status != ATLAS_OK) ||
        snapshot->maintenance_active || snapshot->sensor_recovery_active)
    {
        return; /* Fail closed when state cannot be inspected coherently. */
    }
    if (((snapshot->valid_mask & ATLAS_RTOS_VALID_MS5611) == 0U) ||
        (snapshot->ms5611_status != ATLAS_OK) ||
        !AtlasRtosPolicy_TimestampFresh(now_ms,
                                       snapshot->ms5611.timestamp_ms, 500U))
    {
        return; /* A retained value is not necessarily a current measurement. */
    }
    /* Add pure computation here, with application-specific quality/age limits.
     * Do not call a driver, allocate memory, block, or retain snapshot's pointer. */
}
```

The minimum-delay helper is corrected and tested at scheduler-phase boundaries; physical conversion timing still requires measurement. The 500 ms check mirrors the supervisor's barometer freshness limit, not a control-design recommendation. Algorithms using other data must check those fields' validity, age, quality and body-frame calibration too.

Use `AtlasRtos_SubmitCommand()` for indicators/links and the separate [peripheral service APIs](PERIPHERALS.md) for storage, USB, analog, expansion and qualified outputs. Queue acceptance is not execution success: consume ticketed results. Completion callbacks run in the sensor owner and must remain nonblocking. Never call FatFs, HAL or a device driver directly from this hook. The hook is skipped while outputs are not permitted; use a separately reviewed application/ground-service task if behavior must run during faults or commissioning.

## Safety and source authority

1. Keep the physical pyro arm link open, disconnect energetic loads/motors/servos, and use current-limited bench power during ordinary development. No incidental firing or bypass test mode.
2. Powered outputs must be benign through reset, boot, exceptions, brownout, watchdog recovery and firmware update. Initial GPIO-low configuration is not evidence for all of these cases.
3. A software command alone must never authorize an energetic output. Physical inhibit, validated state, bounded independent pulse control, measurement validity and lockout require a separate reviewed safety design.
4. Unknown electrical, timing or safety assumptions block the dependent feature; do not turn them into defaults.
5. Use as-built measurements/order records and the revision-matched schematic for board wiring, and exact manufacturer specifications/errata for part limits. Net names and documentation cannot override either. Record conflicts.
6. Current behavior is defined by compiled sources, definitions, active linker configuration and binary—not solely `Atlas.ioc`. No build or isolated bench result is flight qualification.

## Code and concurrency standards

- Use C11, fixed-width hardware/serialized fields, explicit units and endian/schema definitions, module-prefixed public names and `static` file-local state. Add no unexplained warnings or suppressions.
- Every project-owned C/header file begins with Doxygen `@file`, purpose, and a major-functions/definitions index. Every function has `@brief`, each applicable `@param`, and `@return` for non-void results; add context, persistence and safety preconditions where relevant.
- Explain complex protocol packing, unusual register maps, synchronization and safe-state transitions inline. Tie hardware constants to primary references in the review.
- Bound every loop, transaction, retry, parser, queue and wait. A timeout must relinquish hardware/buffer ownership and preserve diagnostics. Use `AtlasTime_DelayMs()` for minimum physical waits; its phase guard does not establish an upper response-time bound.
- All RTOS objects and stacks use static allocation; heap growth is prohibited. GNU `_sbrk()` always rejects requests and IAR's HEAP size is zero. App code uses bounded explicit formatting, not libc printf/scanf or allocation calls; inspect the final image for accidentally pulled allocator paths. Do not use `volatile` as a substitute for synchronization.
- After scheduler start, the [task ownership table](reference/RTOS.md#tasks-and-ownership) is authoritative: `AtlasIO` owns sensor/link/expansion operations, while output/ADC, USB and storage have dedicated owners. Algorithms read coherent snapshots and queue bounded commands. Extend complete owner-task operations, not per-byte mutexes that permit semantic interleaving.
- The control response, including dispatch and snapshot wait, must finish within its scheduled 10 ms cycle; hooks must never block or call a driver. A future service needs an owner, budget, static storage, overflow policy, heartbeat, fault handling and measured stack margin.
- ISRs only acknowledge/capture bounded data and defer work. No parsing, filesystem, blocking, allocation or watchdog refresh. Current peripheral ISRs use no kernel API; any future `FromISR` call requires NVIC numerical priority 5–15 under priority group 4 and a fresh yield/priority audit.
- Keep one HAL-plus-kernel SysTick wrapper and one matching SVC/PendSV port. Do not enable a second CMSIS/CubeMX RTOS layer.
- Supervisor faults remain latched until reset. Never refresh the watchdog unconditionally to hide a fault. Freshness, measurement quality, task liveness and deadline compliance are different checks.
- Ordinary boot must not save module settings. Persistent GNSS/BLE/radio/calibration operations require deliberate commissioning and verified post-restart readback where supported.
- DMA buffers must be reachable by the specific bus master, aligned, and owned until verified completion/abort. Any future D-cache enable requires a cache/MPU design, not just pointer alignment. Check both linker maps.
- A clock change requires recalculating every dependent bus, timer, ADC, watchdog and USB clock and measuring the result. Preserve reset causes and retained diagnostics when implementing fault recording.

## Generated code and dependencies

Keep hand-written logic in `App/`; treat `Core/`, `FATFS/`, `USB_DEVICE/`, `cmake/stm32cubemx/` and generator-managed EWARM settings carefully. Preserve `USER CODE` regions. Do not modify vendor trees to hide integration faults; carry any necessary vendor patch explicitly with provenance.

Before regeneration, record a clean baseline and use STM32CubeMX **6.17.0** with STM32Cube FW_H7 **1.13.0** unless upgrading is the reviewed task. Generate into a disposable comparison copy. Review clocks, GPIO states, DMA, IRQs, middleware, source membership, linker files and all manual RTOS hooks before merging. Preserve the explicit main-stack/MPU-guard placement and rejecting `Core/Src/sysmem.c` heap boundary. Build Debug/Release and repeat affected acceptance tests. Dependency versions and update rules are in [Provenance](reference/PROVENANCE.md).

## Change and release checklist

- [ ] Focused branch/changes; scope, assumptions, affected hardware and safety impact explained.
- [ ] Current sources and primary specifications checked; generated changes separated from hand-written changes.
- [ ] Every new App source in GNU and IAR; fresh affected builds, warning/map/stack review.
- [ ] Host regression and review probes run; new fault/timeout/overflow tests added where relevant; unresolved failures disclosed.
- [ ] Measured bench evidence proportional to risk: inert output/fault checks, timing/load/stack tests, or manufacturing revision/ERC/DRC/BOM/CPL evidence.
- [ ] [Systems](SYSTEMS.md) and the current finding record updated; device/RTOS/hardware details edited only at their canonical location.
- [ ] No credentials, private data, machine-specific paths, build products, raw KiCad sources or obsolete origin documents committed.
- [ ] At least one reviewer; **two qualified reviewers for safety-relevant changes**, including clocks, memory, interrupts, boot, watchdog and outputs. Author self-reviews are not independent approvals.

Tag a release only from an approved clean source state. Retain tool/dependency versions, build output, maps, binary hashes, requirements and traceable test evidence. Never label an image flight-ready or qualified without the responsible owner's documented release decision. Do not resolve a substantive safety objection without evidence or agreement from its reviewer.
