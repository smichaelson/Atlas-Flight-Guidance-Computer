# Atlas Engineering and Collaboration Standards

These standards apply to firmware, generated configuration, documentation, hardware references, manufacturing artifacts, testing, and releases in this repository. They are intentionally conservative because Atlas controls powered outputs and includes physically armed event channels.

## 1. Non-negotiable safety rules

1. Treat the repository as development software until a documented verification program says otherwise. A successful compile or bench boot is not flight qualification.
2. Do not connect an igniter, motor, flight battery, or other energetic load during ordinary development. Use unarmed hardware, current-limited bench power, and inert dummy loads.
3. Event-output gates, PWM outputs, and externally powered control outputs must remain benign through reset, boot, initialization, watchdog recovery, clock failure, exceptions, brownout, and firmware update.
4. A software command alone must never establish permission to energize an event output. Physical arming state, validated system state, bounded pulse duration, continuity interpretation, and post-fire lockout must all be explicit.
5. Never add a test mode that bypasses safety checks in a release-capable image. Hardware-in-the-loop and factory modes must be deliberate, visible, bounded, and disabled by default.
6. If a safety assumption is unknown, record it as an open item and block the dependent function. Do not silently convert an unknown into a default.

## 2. Source-of-truth and change-control policy

For hardware facts, use this order:

1. Assembled board measurements and confirmed as-built order records.
2. The revision-matched schematic export in `hardware/`.
3. Revision-matched Gerbers, drill files, BOM, and placement files.
4. Current component-manufacturer data sheets and errata.
5. Repository documentation.
6. Net names, generated labels, and informal notes.

For current firmware behavior, use the compiled source set, active linker script, compiler definitions, and build configuration. `Atlas.ioc` describes intended STM32CubeMX state; it is not proof that the generated tree or binary matches it.

Any unresolved conflict must be documented in the issue or pull request and resolved before merge when it affects electrical limits, memory safety, timing, or output behavior.

## 3. Generated and project-owned code

- Treat `Core/`, `FATFS/`, `USB_DEVICE/`, `cmake/stm32cubemx/`, and portions of `EWARM/` as generated or generator-managed.
- Keep hand-written application logic in clearly project-owned modules. As the application grows, prefer explicit top-level areas such as `App/`, `Board/`, `Services/`, and `Tests/` instead of accumulating logic in `main.c`.
- Do not edit `Drivers/` or third-party `Middlewares/` to implement application behavior. Wrap vendor code or carry a clearly documented, minimal patch when an upstream defect leaves no alternative.
- Preserve STM32CubeMX `USER CODE` markers. Hand edits outside protected regions must either be reproducible after generation or be called out in the pull request.
- Add every new source file to both supported build systems, or explicitly document that a build system is no longer supported.

### Regeneration procedure

Before regenerating from `Atlas.ioc`:

1. Start from a clean branch and record the current build result.
2. Use STM32CubeMX 6.17.0 and STM32Cube FW_H7 1.13.0 unless a version upgrade is the reviewed purpose of the change.
3. Generate into a disposable comparison copy first.
4. Review the complete generated diff, including clock trees, GPIO reset levels, NVIC entries, DMA requests, middleware, linker inputs, and build membership.
5. Merge only the intended changes, then build both Debug and Release configurations.
6. Re-run affected bench tests and record the tool versions and evidence in the pull request.

## 4. C and embedded-software conventions

- The common language baseline is C11. New code must compile without new warnings under the supported Arm GNU and IAR configurations.
- Use fixed-width integer types for hardware-facing data and serialized formats. State units in names or types, such as `timeout_ms`, `period_us`, `voltage_mv`, and `temperature_cdeg`.
- Use descriptive module prefixes for externally visible symbols. Keep file-local functions and state `static`.
- Avoid dynamic allocation after initialization. Any exception requires a bounded lifetime, failure path, fragmentation analysis, and tests.
- Bound every loop, wait, retry, queue, parser, and hardware transaction. Timeouts must lead to a defined diagnostic and safe state.
- Do not use `volatile` as a synchronization primitive. Shared ISR/main-context state needs an atomic operation, short critical section, or documented lock-free pattern.
- Interrupt handlers may acknowledge hardware, capture bounded data, timestamp, and signal deferred work. They must not parse protocols, format text, access a filesystem, block on a bus, allocate memory, or refresh the watchdog.
- Use `assert` for programmer invariants in test/debug builds, not for recoverable hardware faults. Production faults must follow an explicit safe-state and diagnostic path.
- Keep hardware register magic values tied to a named constant and a data-sheet or reference-manual citation in the code review description.

## 5. Startup, watchdog, clocks, and memory

- Establish safe GPIO output levels before changing pins to output or alternate-function mode. Event outputs default low; SPI chip selects default inactive; PWM channels remain disabled until an application command is validated.
- The watchdog must be refreshed only by a health supervisor that has evidence all required tasks met their deadlines. Do not refresh it unconditionally in the main loop or an interrupt.
- A clock change requires a documented clock tree and recalculation of flash latency, bus limits, timer frequencies, UART baud rates, I2C timing, SPI rates, ADC clocks, SDMMC, and the exact 48 MHz USB clock.
- Verify the clock tree at runtime and with at least one independent timing measurement during bring-up.
- DMA buffers must be placed in DMA-accessible memory and, when the data cache is enabled, follow a documented MPU or cache-maintenance policy with 32-byte cache-line alignment.
- Review both GNU and IAR linker maps. A linked image is not accepted until flash/RAM ranges, stack, heap, DMA buffers, and safety-critical storage are within their intended regions.
- Record reset causes before clearing them. Fault and watchdog resets must leave enough retained diagnostic information to identify the failing stage without compromising safe outputs.

## 6. Hardware drivers and data integrity

- Confirm each exact fitted part number, bus mode, address, reset behavior, startup delay, maximum clock, and identity register from current primary-vendor documentation.
- Verify sensor axis transforms on assembled hardware. Placement-file rotation alone is not sufficient evidence.
- Every driver needs explicit initialization, identity checking, configuration readback where available, timeout handling, health counters, and a defined failed/degraded state.
- Serialize access to shared buses and keep unrelated chip selects inactive.
- Timestamp measurements from a monotonic hardware timebase. Serialized data formats must carry an explicit schema version, units, byte order, and integrity check.
- Storage code must tolerate power loss. Define file creation, flush/sync policy, recovery, capacity limits, safe removal, and behavior when media is absent or corrupt.

## 7. Review and Git workflow

- Use short-lived branches named by intent, for example `feature/...`, `fix/...`, `docs/...`, or `hardware/...`.
- Keep commits focused and explain why the change is needed. Do not mix generated churn, formatting-only changes, and functional changes without a clear reason.
- Pull requests must state scope, affected hardware, risk, tests, observed results, and remaining limitations.
- At least one reviewer is required. Two qualified reviewers are required for safety-relevant changes, including power, clocks, memory placement, interrupts, watchdogs, actuators, event outputs, boot, and fault handling.
- The author must not resolve a substantive safety objection without recorded evidence or explicit agreement from the reviewer who raised it.
- Do not commit build products, editor caches, credentials, private keys, machine-specific absolute paths, or raw measurement data containing personal information.

## 8. Verification expectations

Every functional pull request must provide evidence proportional to risk:

- **Documentation only:** link and terminology check; verify claims against the current source or schematic.
- **Generated configuration:** Debug and Release builds, full generated diff review, linker-map review, and affected peripheral smoke tests.
- **Driver or service:** unit tests where practical, fault-injection tests, timeout/recovery tests, and bench evidence on inert hardware.
- **Timing or control:** measured rates, jitter, worst-case execution time, deadline behavior, and watchdog interaction.
- **Power or output behavior:** current-limited bench setup, safe-state checks across reset/fault paths, and a written test procedure approved before execution.
- **Manufacturing change:** schematic/PCB revision alignment, ERC/DRC evidence, Gerber review, BOM/CPL review, polarity/orientation review, and an identified fabrication package.

Tests must report tool and hardware revisions, setup, inputs, expected result, observed result, pass/fail, and retained evidence. "It worked once" is not a repeatable test.

## 9. Documentation and release discipline

- Update `docs/PROJECT_STATUS.md` whenever implemented capability or a known blocker changes.
- Update `docs/HARDWARE_OVERVIEW.md` and `hardware/README.md` when the hardware revision or authoritative package changes.
- Record exact dependency and generator versions for each release candidate.
- Do not label a binary flight-ready, qualified, or safe without an approved requirements set, traceable verification evidence, and a release decision by the responsible project owner.
- Tag releases only from a clean tree. Retain the source revision, submodule/dependency state, tool versions, compiler output, linker maps, binary hashes, and test report.

## 10. Completion checklist for a pull request

- [ ] Scope and safety impact are stated.
- [ ] Source authority and assumptions are identified.
- [ ] Generated and hand-written changes are distinguishable.
- [ ] Debug and Release builds pass for each supported toolchain affected.
- [ ] Warnings are not added or suppressed without explanation.
- [ ] Linker-map and memory-placement effects are reviewed where applicable.
- [ ] Automated and bench tests are documented with results.
- [ ] Reset, timeout, fault, and safe-output behavior are covered where applicable.
- [ ] Documentation and revision identifiers are updated.
- [ ] No secrets, personal absolute paths, build outputs, or unintended binaries are included.
- [ ] Required reviewers have approved the change.

