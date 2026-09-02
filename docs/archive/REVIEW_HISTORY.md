# Historical review archive

These six sequential author self-reviews describe earlier repository states. Their **pass conclusions, build figures, timing claims and completeness claims are superseded** by the [current review report](../REVIEW_REPORT.md) and [Systems](../SYSTEMS.md). They are not independent reviews or hardware qualification.

Historical text is retained below; links are rebased to the current documentation layout. Follow Git commit `0dbc245` to recover the exact original files and original link destinations. The archived prose is evidence of what was previously claimed, not current operational guidance.

- [Review 1](#review-1)
- [Review 2](#review-2)
- [Review 3](#review-3)
- [Review 4](#review-4)
- [Review 5](#review-5)
- [Review 6](#review-6)

## Review 1

### Review 1 — Hardware and Protocol Conformance

| Field | Value |
|---|---|
| Date | 2026-09-01 |
| Baseline | Atlas Flight Computer schematic Rev. 0.1, dated 2026-03-19 |
| Scope | Fitted sensors, GNSS, NINA-B112, external RFD900x interface, RGB LED, and differential buzzer |
| Method | Independent pass over schematic-derived nets, generated peripheral configuration, driver transactions, and primary manufacturer manuals |
| Outcome | Pass for implementation consistency; physical acceptance remains on hold |

#### Questions applied

1. Does the code address the exact fitted part, bus instance, address/chip select, interrupt, reset, and output polarity shown by the hardware reference?
2. Are startup commands, register encodings, byte order, checksums, reset delays, mode transitions, and configuration readbacks consistent with the primary manufacturer interface documents?
3. Can an acknowledgement, echo, timeout, or stale software flag be mistaken for proof of active configuration?
4. Does ordinary boot avoid unrequested nonvolatile writes and unsafe output activity?

#### Coverage and result

| System | Conformance focus | Result |
|---|---|---|
| ADXL375 | SPI2 mode 3, PG12 select, `0xE5` identity, fixed format, ODR, data-ready polling, XYZ little-endian burst | Source consistent; register emulator passed; bench pending |
| LSM6DSV16B | SPI3 mode 3, PG10 select, PG2 INT1, `0x71` identity, reset, ODR/FS, direct-output map | Source consistent; register emulator passed; bench pending |
| MMC5983MA | SPI2 mode 3, PG11 select, `0x30` identity, control-bit encodings, 18-bit packing, SET/RESET | Source consistent; register emulator passed; bench pending |
| MS5611-01BA03 | I2C1 address `0x77`, reset/PROM sequence, CRC-4, conversion commands, signed 64-bit compensation | Source consistent; command/math emulator passed; bench pending |
| BNO085 | I2C1 address `0x4B`, PB13 reset, PG0 interrupt, CEVA SHTP/SH-2 adapter, report IDs and reset handling | Source and pinned library consistent; physical SHTP trace pending |
| NEO-M9N | USART1, TIM2 PPS capture, UBX checksum, MON-VER, NAV-PVT offsets, RAM CFG keys and typed values | Source consistent; parser/config/PPS tests passed; bench pending |
| NINA-B112-05B | USART6 RTS/CTS, normal boot straps, reset/DSR/DTR polarity, R55 AT syntax, SPS role/server/start mode | Source consistent; AT/profile/mode tests passed; bench pending |
| RFD900x header | USART3 transparent transport, SiK escape guards, local AT access, readback before save | Source consistent; AT emulator passed; modem/region/paired link pending |
| RGB LED | PB6/PB7/PD14 low-side transistor gates and active-high logical colors | Runtime GPIO ownership and logic test passed; electrical color/polarity pending |
| Buzzer | PE5/PE6 TIM15 differential drive, opposite PWM modes, bounded frequency and silent stop | Timer contract test passed; waveform/load measurement pending |

#### Findings resolved in this baseline

- **R1-01 — Radio terminology:** The J9 interface had been described informally as LoRa. Hardware and RFDesign documentation identify an external RFD900x/SiK frequency-hopping serial modem; code and documentation now use `RFD900x` and explicitly reject a LoRa/LoRaWAN claim.
- **R1-02 — GNSS configuration proof:** A CFG-VALSET acknowledgement alone was insufficient. Initialization now performs CFG-VALGET against RAM and requires all six typed keys to match, independent of response order.
- **R1-03 — BLE operating mode:** u-connectXpress R55 defines `UMSM=1` as ordinary Data mode and `UMSM=2` as Extended Data Mode. The saved SPS profile uses `UMSM=1`; wired `AT&D1` command entry requires the unsolicited `OK` before a fresh AT probe.
- **R1-04 — BLE persistence:** Persistent configuration now uses `AT&W`, restart, complete profile re-read, an operational DSR transition check, and verified `ATO1` return rather than treating write acknowledgements as durable proof.
- **R1-05 — LSM6DSV16B output order:** The direct accelerometer registers are Z, Y, X at `0x28`, `0x2A`, `0x2C`; the driver and test retain this non-obvious map explicitly.
- **R1-06 — LED pin ownership:** CubeMX stages the LED pins as TIM4 alternate functions. `AtlasLed_Init()` now establishes fail-dark output levels and deliberately reclaims all three pins as GPIO before color control.
- **R1-07 — BNO085 asynchronous faults:** CEVA transport and reset counters are surfaced through the adapter; newly observed protocol/I/O/decode/reset faults latch the board runtime fault instead of being silently ignored.

#### Primary references

The module guides under [Module Firmware Guides](../README.md#device-references) link the exact Analog Devices, ST, MEMSIC, TE Connectivity, CEVA, u-blox, RFDesign, and Murata primary documents used for this pass. No secondary tutorial was treated as protocol authority.

#### Residual hold points

- No assembled Atlas board was available to confirm population, rework state, bus electrical behavior, interrupt polarity, reset timing, output polarity, or sensor orientation.
- No BNO085 SHTP trace, GNSS fix/PPS measurement, BLE RF/SPS session, or RFD900x paired-radio session has been captured.
- Nominal sensor scaling is not calibration, and no body-frame transform is approved.
- Passing this review therefore supports implementation and host-protocol status only. Follow [Module Bring-up and Acceptance](../SYSTEMS.md) before promoting any system to bench-verified.

## Review 2

### Review 2 — Implementation, Failure Paths, and Builds

> Applicability note (2026-09-01 RTOS integration): references below to the former foreground main loop are historical. Review 4 supersedes that execution model with a highest-priority RTOS supervisor as the sole watchdog owner.

| Field | Value |
|---|---|
| Date | 2026-09-01 |
| Scope | Project-owned firmware, CEVA integration, target build inputs, host tests, bounded-failure behavior, and memory evidence |
| Method | Fresh source pass, strict host compile/tests, clean Debug/Release Arm GNU builds, source-membership/XML checks, API scan, and compiler stack-frame inspection |
| Outcome | Pass for the tested toolchain; IAR compilation and hardware execution remain pending |

#### Questions applied

1. Are externally supplied lengths, indices, parser states, queues, waits, and response buffers bounded and fail-visible?
2. Do interrupts remain short, with protocol parsing and blocking bus work deferred to foreground context?
3. Can a partial response, command echo, asynchronous reset, ring overflow, or secondary-channel failure be reported as success?
4. Do runtime failures reach the board-level fault latch and prevent unconditional watchdog refresh?
5. Are all project-owned and pinned CEVA units present in both supported build descriptions?

#### Findings resolved in this baseline

- **R2-01 — BLE response truncation:** A caller response buffer overflow was counted but could previously be followed by terminal `OK`. `AtlasBle_Command()` now returns `ATLAS_ERROR_OVERFLOW` immediately for either line or destination truncation; a regression test proves `command_ok` remains unchanged.
- **R2-02 — Radio echo-only evidence:** `ATI`/`ATI5` terminal `OK` with only local command echo did not prove an identity/settings response. The typed helpers now require substantive non-echo content and increment `malformed_responses` on failure.
- **R2-03 — Coherent PPS reads:** Foreground PPS consumers use a short critical-section snapshot so an interrupt cannot produce a mixed capture/period/count structure.
- **R2-04 — Partial buzzer start:** If TIM15 channel 2 fails, channel 1 is stopped before the driver returns an error; stop remains idempotent.
- **R2-05 — Runtime service visibility:** BNO085 service/counter changes, UART service failures, and buzzer service failures latch `AtlasBoard.runtime_fault`; the main loop stops refreshing the watchdog after a startup or runtime fault.

#### Automated evidence

| Check | Result |
|---|---|
| Host C11 compile | Passed with MinGW-w64 GCC 13.1.0, `-Wall -Wextra -Werror` |
| Host protocol suite | Passed all assertions, including direct sensor register/command emulators, GNSS, BLE, RFD900x, LED, buzzer, and UART overflow |
| Arm GNU Debug clean build | Passed without compiler warnings; 121,412 bytes flash, 31,440 bytes DTCM |
| Arm GNU Release clean build | Passed without compiler warnings; 59,908 bytes flash, 31,440 bytes DTCM |
| Project source membership | All 14 `App/Src/*.c` units are present in CMake and IAR; all four pinned CEVA `.c` units are present in both |
| IAR project structure | `EWARM/Atlas.ewp` parses as XML and includes the required source/include paths |
| Dynamic/unsafe API scan | No project-owned `malloc`, `calloc`, `realloc`, `free`, `HAL_MAX_DELAY`, `sprintf`, `strcpy`, or `strcat` use found |
| Compiler stack-frame files | Largest observed project-owned single frame: 568 bytes in `AtlasGnss_SendUbx`; GNU linker reserves 16 KiB stack and zero heap |

The stack-frame inspection is not a whole-program call-chain or interrupt-nesting proof. It is evidence for targeted review, not a worst-case stack guarantee.

#### Design observations

- UART receive paths are allocation-free single-producer/single-consumer rings. ISRs capture bytes/errors and restart reception; foreground code parses protocols.
- SPI chip-select scope covers complete transactions. No dynamic allocation is used by the Atlas application layer.
- Device waits, conversion polls, command transactions, and startup probes use explicit time bounds. The intentional infinite loops are the main foreground scheduler and generated fatal `Error_Handler()` fail-stop.
- Ordinary startup performs no RFD900x AT command, no BLE save, and no GNSS nonvolatile write.
- Configuration APIs separate volatile setup from explicit persistence and document the NVM side effect.

#### Residual hold points

- IAR Embedded Workbench was unavailable, so XML/source membership was checked but no IAR object or map was produced.
- There is no static whole-call-graph stack proof, measured worst-case execution time, scheduler deadline analysis, or hardware-in-the-loop run.
- Host HAL emulators prove software handling of defined transactions; they do not prove electrical timing, silicon behavior, clock accuracy, or recovery from every physical fault.
- Independent qualified review remains required by [Engineering and Collaboration Standards](../DEVELOPMENT.md), especially before output, watchdog, interrupt, or control behavior is released.

## Review 3

### Review 3 — Documentation, Onboarding, and Repository Hygiene

| Field | Value |
|---|---|
| Date | 2026-09-01 |
| Scope | New-collaborator path, claim/evidence consistency, module guides, code documentation, links, forbidden artifacts, and generated-output hygiene |
| Method | Read-through from a new collaborator's perspective plus repeatable repository checks |
| Outcome | Pass for repository/document consistency; external URLs and physical procedures require ongoing maintenance |

#### Questions applied

1. Can a new collaborator determine what Atlas is, what is implemented, what is unverified, where each API lives, and how to build/test without relying on oral history?
2. Does every fitted sensor, major communications module, and user-feedback output have a guide containing wiring, startup behavior, API contract, units/format, health interpretation, known limits, primary references, and bench acceptance?
3. Do documentation claims distinguish protocol-tested, target-built, bench-verified, and flight-qualified status?
4. Are project-owned firmware files headed by purpose/major-function indexes, and are functions documented with Doxygen data tags and important inline rationale?
5. Are raw KiCad sources, obsolete Atlas_Origins material, build products, machine-local paths, and broken local links absent from the deliverable?

#### Findings resolved in this baseline

- **R3-01 — Stale validation descriptions:** The validation ledger and sensor/output guides understated the expanded direct-register, command-flow, LED, and buzzer host tests. The matrix now names the exact tested behavior without claiming physical verification.
- **R3-02 — Stale memory figures:** Build documentation was synchronized to the final clean images: Debug 121,412 bytes flash / 31,440 bytes DTCM and Release 59,908 bytes flash / 31,440 bytes DTCM.
- **R3-03 — Header consistency:** The shared status files now use the same “Major functions” convention as other project-owned units, and the generated `main.c` user header lists its Atlas integration responsibilities.
- **R3-04 — Failure semantics documentation:** BLE response overflow and RFD900x echo-only identity/settings rejection are now stated in API comments, module health tables, tests, and status evidence.

#### Onboarding path reviewed

The intended sequence is [README](../../README.md) → [Project Status](../SYSTEMS.md) → [Engineering Standards](../DEVELOPMENT.md) → [Module Firmware Guides](../README.md#device-references) → [Firmware Architecture](../reference/RTOS.md) → [Building](../DEVELOPMENT.md) → [Bring-up](../SYSTEMS.md) → [Validation Status](../SYSTEMS.md).

The path was checked for consistent vocabulary, safety boundaries, source authority, build prerequisites, debugger entry points, module ownership, persistent side effects, and handoff to physical acceptance.

#### Repeatable checks

Run from the repository root:

```text
powershell -ExecutionPolicy Bypass -File Tests/repository/check_repository.ps1
```

The check resolves every local Markdown target, requires the core onboarding/review documents, verifies Doxygen file/major-function headers and `@brief`/`@param`/`@return` tags for `App/` definitions, and rejects raw KiCad source extensions, Atlas_Origins paths, target build artifacts outside excluded build directories, and related repository debris. It passed for this reviewed snapshot.

Additional pass-specific scans found no project-owned use of machine-local `C:\Users\...` links and no stale prior memory figures. Occurrences of “LoRa” remain only where the documentation explicitly corrects that misconception.

#### Deliverable policy confirmed

- No raw KiCad schematic/PCB/project file is included.
- No file or directory named for obsolete Atlas_Origins material is included.
- The revision-matched schematic PDF, BOM, and candidate manufacturing outputs remain clearly labeled; they are not presented as verified as-built provenance.
- Generated build directories, ELF/HEX/BIN/map/object/stack-usage files are not part of the copy set.
- The source tree, module guides, tests, standards, review evidence, and supported build membership are included together.

#### Residual hold points

- External manufacturer links were selected from primary sources, but upstream URLs can move and should be checked during release preparation.
- Documentation cannot validate the as-built board, radio region, module firmware, calibration, timing, or fault recovery; each module guide deliberately ends in a physical acceptance procedure.
- These records are self-review evidence. They do not replace independent human review, requirements traceability, or flight qualification.

## Review 4

### Review 4 — RTOS Architecture, Timing, and Safety Supervision

#### Review identity

- **Review performed:** 2026-09-01
- **Scope:** FreeRTOS selection/configuration, task graph, ownership boundaries, interrupt contract, timing/deadline logic, sensor freshness, and independent-watchdog policy
- **Method:** line-by-line design review, call-site searches, target symbol inspection, deterministic host-policy tests, and comparison against the locally installed STM32CubeH7 FreeRTOS source
- **Result:** pass after the corrections recorded below

This is a rigorous author self-review, not an independent qualified avionics, functional-safety, hardware, or flight-readiness review.

#### Reviewed invariants

1. Exactly one post-start task owns each driver and complete bus transaction.
2. Interrupts perform only bounded flag, capture, and UART-ring work and call no FreeRTOS API.
3. Every scheduler object and stack has static application storage; no heap implementation is linked.
4. The application receives coherent copies and submits bounded commands instead of touching hardware.
5. The watchdog is refreshed by one highest-priority task only after positive evidence of liveness, status, freshness, deadline, and stack health.
6. All time comparisons remain correct across the 32-bit millisecond counter rollover.
7. Deliberately long radio/BLE maintenance calls cannot disguise a task stall or defer freshness indefinitely.

#### Evidence examined

- FreeRTOS Kernel V10.6.2 common sources and Cortex-M7 r0p1 GNU/IAR ports under `ThirdParty/FreeRTOS-Kernel/`.
- SHA-256 comparison of all 36 copied vendor files (excluding the Atlas-authored local README) against `STM32Cube_FW_H7_V1.13.0/Middlewares/Third_Party/FreeRTOS/Source`; every file matched.
- `FreeRTOSConfig.h`: 1 kHz preemption, static-only allocation, FPU enabled, stack checks, priority 15 kernel exceptions, and maximum syscall priority 5.
- `main.c`, `stm32h7xx_it.c`, startup vector behavior, NVIC configuration, and the GNU/IAR port membership.
- All project-owned task, queue, mutex, stream, delay, driver, callback, and watchdog call sites.
- Host tests for ordinary and wrapped periodic/freshness calculations and every supervisor decision branch, including active/expired long operations.

#### Findings and corrections

| ID | Severity | Finding | Correction and disposition |
|---|---|---|---|
| R4-01 | High | A prior supervisor decision could resume IWDG refresh after a fault had already latched. | The first `AtlasRtosHealth.fault` now overrides every later policy result; feeding can never resume before reset. Closed. |
| R4-02 | High | BLE, GNSS, RFD, and BNO polling/retry paths could consume a full timeout without yielding, starving the control task despite preemption intent. | All project driver waits use `AtlasTime_DelayMs()`, which becomes `vTaskDelay()` after scheduler start; busy polling now yields at 1 ms intervals. Closed. |
| R4-03 | Medium | A receive caller could spend its timeout once acquiring the reader mutex and again waiting for stream data. | Reader serialization is nonblocking; only the stream receive consumes the caller's bounded wait. Closed. |
| R4-04 | High | The control heartbeat advanced even if no coherent snapshot/hook cycle completed, weakening stall detection. | The heartbeat now advances only after snapshot acquisition and hook return. Closed. |
| R4-05 | High | Allowing both supervisor and I/O task to write `AtlasBoard.runtime_fault` created an unsynchronized C data race. | Board runtime state is single-writer I/O-owned; supervisor-only faults stay in mutex-protected RTOS health. Closed. |
| R4-06 | High | Task heartbeats and successful status codes did not prove sensors continued producing data. A permanently deasserted DATA_READY line could leave stale values marked valid while IWDG was fed. | Added required-sensor validity/age supervision, wrap-safe timestamp checks, a two-second startup grace, `SENSOR_STALE`, and a latched offending-bit mask. Closed pending bench rate validation. |
| R4-07 | High | A valid long radio/BLE transition paused sampling, so freshness would fault before the reviewed transition deadline. The exception also failed when an I/O heartbeat happened to advance during the same supervisor window. | The policy recognizes any active, in-deadline maintenance operation independently of heartbeat change; it defers freshness only for that bounded interval while still requiring application progress and stack health. Closed. |
| R4-08 | High | Clearing the long-operation marker immediately on return created a race: the supervisor could inspect stale pre-maintenance samples before the I/O task replenished them. Repeated transitions could also defer freshness forever. | The I/O task publishes maintenance inhibit state, then exactly one 1,200 ms recovery window. A second long transition is rejected during recovery; I/O liveness and error statuses remain enforced. Closed pending traffic/overflow bench tests. |
| R4-09 | High | A long call could exceed its deadline and return between 100 ms supervisor checks, escaping `IO_DEADLINE`. | The I/O task rechecks the saved deadline on return and latches the same fault before clearing busy state. Closed. |
| R4-10 | High | The 100 Hz control hook could overrun every 10 ms period yet keep changing its 100 ms heartbeat. | The complete snapshot-copy-plus-hook duration now latches `APPLICATION_DEADLINE` at 10 ms and records misses. Closed pending fine-grained bench timing. |
| R4-11 | Medium | Assertion/stack-overflow diagnostics were written before interrupts were disabled, leaving a narrow opportunity for one last supervisor refresh during partial capture. | Both fatal hooks disable interrupts before writing fault context. Closed. |

#### Interrupt and priority conclusion

The project requires `NVIC_PRIORITYGROUP_4`. SysTick and PendSV run at numerical priority 15; the maximum FreeRTOS syscall priority is 5. Existing priority 0–4 callbacks only set flags/counters or copy capture bytes and call no `...FromISR()` API, so they are legal above the kernel masking threshold. UART interrupt priorities are 6 and also call no RTOS API. Any future ISR-to-RTOS call requires a new priority audit.

The project-owned SysTick wrapper advances the HAL millisecond tick and calls the kernel tick only after scheduling starts. SVC and PendSV bodies come from the selected FreeRTOS port, avoiding duplicate handlers.

#### Open verification risks

- No assembled Atlas board was available in this review. Sensor rates, bus latency, UART/SHTP backlog recovery, interrupt loading, control jitter, and fault-reset behavior remain unmeasured.
- IWDG timing is derived from generated prescaler/reload settings and the nominal LSI frequency; oscillator tolerance and the real reset interval must be measured on each supported hardware revision.
- Radio/BLE mode transitions are explicitly maintenance-only. The application must inhibit control while `maintenance_active` or `sensor_recovery_active` is true.
- The 64-word live stack threshold and static stack allocations are conservative starting values, not proven worst-case margins.
- Fault context is not retained across reset; reset-cause/crash persistence remains future work.

#### Conclusion

The RTOS design is internally coherent and fail-closed at source/policy level after correction of the findings above. It is suitable as an integration foundation, but it is not yet hardware- or flight-validated. Review 5 separately verifies implementation/build evidence; Review 6 audits documentation and repository readiness.

## Review 5

### Review 5 — RTOS Implementation, Concurrency, Build, and Memory

#### Review identity

- **Review performed:** 2026-09-01
- **Scope:** project-owned RTOS implementation, task/ISR concurrency, queues and streams, fault paths, static memory, source membership, host tests, Arm builds, and linked-image symbols
- **Method:** fresh line-by-line source pass, lock/owner and preemption analysis, bounded-loop search, strict host tests, clean Debug/Release Arm GNU builds, GCC static analysis, linker-symbol/disassembly inspection, compiler stack-usage review, IAR XML/path inspection, and vendor-file hashing
- **Result:** pass at source/build level after the corrections below; IAR compilation and physical execution remain pending

This is a rigorous author self-review, not an independent qualified reviewer or hardware acceptance test.

#### Questions applied

1. Can any task, callback, or hook concurrently enter a driver or split a bus/module transaction?
2. Can a queue, mutex, stream, parser, retry, or interrupt-driven snapshot block or retry without a fixed bound?
3. Can any ordering window make the supervisor accept stale data, report a false stall, or resume watchdog refresh after a fault?
4. Are all scheduler resources fixed-size, correctly aligned/provisioned, and represented in both supported build systems?
5. Does the linked image contain exactly one exception implementation, one watchdog refresh call site, and no heap allocator?
6. Are stack allocations defensible from compiler evidence and guarded by live high-water supervision?
7. Are test/build claims limited to what the available toolchains and hardware actually prove?

#### Findings and corrections

| ID | Severity | Finding | Correction and disposition |
|---|---|---|---|
| R5-01 | High | A queued LED, buzzer, radio, or BLE request could otherwise reach hardware after startup/service/sampling or supervisor health had already failed. | The I/O owner checks running state, the monotonic RTOS fault, and all three aggregate statuses after dequeue. A rejected request completes with `ATLAS_ERROR_STATE` and does not call a driver. Closed; an operation already in progress remains bounded by its reviewed call/deadline contract. |
| R5-02 | High | At long-operation completion, clearing `io_busy` before publishing sensor recovery left a preemption window in which the supervisor could see neither a maintenance exception nor a fresh I/O heartbeat and diagnose a false stall/stale condition. | Recovery state is now published first. Completion advances the I/O heartbeat in the same health-mutex section that clears the busy gate, so every supervisor observation sees a valid side of the handoff. Closed pending bench transition stress. |
| R5-03 | Medium | The implementation and README claimed every task stack margin was supervised, but the static FreeRTOS idle task was covered only by overflow checking. | Enabled idle-task-handle access, added its high-water field to policy/health, and require the same 64-word minimum as the three Atlas tasks. Added the low-idle-stack host-policy case. Closed pending live high-water measurement. |
| R5-04 | Medium | The GNSS PPS coherence reader used an unbounded retry loop. Normal 1 Hz input made starvation implausible, but a noisy capture input violated the repository’s bounded-loop rule. | Limited the lock-free copy to three attempts and fail closed for that publication; the next I/O cycle retries naturally. PPS is not a watchdog-required sensor. Closed. |
| R5-05 | Medium | A redundant request to enter a module mode already active could unnecessarily open a long-operation/recovery interval and defer freshness. | Long-operation deadlines and recovery are now opened only for an actual BLE/RFD state transition; already-satisfied requests use the driver’s immediate idempotent path. Closed. |
| R5-06 | Medium | A long transition could return after its deadline between supervisor periods, and fatal diagnostic writes could be preempted before watchdog feeding stopped. | The I/O task checks the saved deadline again on return. Assertion and stack-overflow hooks disable interrupts before recording available context. Closed; also traced in [Review 4](REVIEW_HISTORY.md#review-4). |
| R5-07 | Low | Build parity and static-allocation rules depended on manual inspection alone. | The repository checker now rejects heap implementations, direct App-driver `HAL_Delay()`, and unreviewed `...FromISR()` calls; requires RTOS documents/files; verifies all App sources and kernel/port inputs in CMake/IAR; rejects cross-selected ports; parses the IAR XML; and resolves every referenced IAR file. Closed. |

#### Build and test evidence

- Host suite: all deterministic protocol/math and RTOS-policy checks pass under MinGW-w64 GCC 13.1.0 as C11 with `-Wall -Wextra -Werror`.
- Arm GNU 14.3.1 Debug: complete image compiles and links without warnings; 146,344 bytes flash and 51,976 bytes DTCM in the reviewed build.
- Arm GNU 14.3.1 Release: complete image compiles and links without warnings; 74,064 bytes flash and 51,976 bytes DTCM in the reviewed build.
- Strict project scope: all 17 `App/Src` units plus `main.c` and `stm32h7xx_it.c` pass `-Wall -Wextra -Werror` and GCC `-fanalyzer`. The three new RTOS units also pass `-Wshadow -Wformat=2 -Wdouble-promotion` as errors.
- The normal full-image `-Wall` build is clean. A broader extra-warning experiment reports findings in unchanged generated/vendor headers and USB/FatFs sources; those files are intentionally outside the stricter project-owned claim.
- Linked Release symbols contain exactly one `SVC_Handler`, `PendSV_Handler`, `SysTick_Handler`, and `xPortSysTickHandler`. Disassembly resolves the image’s sole call to `HAL_IWDG_Refresh()` to `atlas_rtos_supervisor_task`.
- No `malloc`, `free`, `pvPortMalloc`, or `vPortFree` symbol is linked, and no `heap_*.c` exists in the vendored kernel subset.
- Release disassembly retains a call from `atlas_rtos_application_task` to the weak `AtlasRtos_ApplicationStep`, so a strong application override is not optimized away.
- All 17 App sources are present in CMake and IAR. The IAR project XML parses and all 84 referenced file paths exist; the compiler was unavailable, so this is not an IAR build claim.
- SHA-256 comparison against the local STM32CubeH7 1.13.0 installation found all 36 copied FreeRTOS vendor files identical; the Atlas-authored README is excluded from that count.

#### Memory and stack review

All tasks, stacks, queue storage, mutex control blocks, and stream-buffer storage are application-static. The stream arrays provide 1,025 physical bytes each to the FreeRTOS ring implementation, yielding the documented 1,024-byte usable capacity.

Debug compiler direct-frame output reports:

| Task entry | Direct frame | Static stack | Interpretation |
|---|---:|---:|---|
| `atlas_rtos_io_task` | 40 bytes | 8,192 bytes | Deep driver callees dominate; direct size alone is not a call-depth bound. |
| `atlas_rtos_application_task` | 632 bytes | 4,096 bytes | The future control-hook call tree must fit inside the same task stack. |
| `atlas_rtos_supervisor_task` | 696 bytes | 2,048 bytes | Includes four high-water queries and coherent policy inputs. |
| FreeRTOS idle | kernel-dependent | 1,024 bytes | Performs no Atlas hook; live margin is now supervised. |

Notable project callee frames include 568 bytes for `AtlasGnss_SendUbx`, 392 bytes for `AtlasMs5611_Compensate`, and 328 bytes for `AtlasBle_Command`. These figures are compiler-local frames, not whole-call-chain proofs. The 64-word supervisor threshold and the initial allocations remain hypotheses until live high-water marks are retained under worst-case traffic and fault paths.

#### Residual limits

- No IAR compiler was available; only project structure, options, XML, and referenced files were checked.
- Address/undefined-behavior sanitizers were unavailable in the installed MinGW toolchain, so no sanitizer result is claimed.
- No assembled Atlas board was available. Bus electrical behavior, module firmware compatibility, throughput/backlog behavior, timing/jitter, live stack margins, and watchdog reset timing remain governed by [Module bring-up and acceptance](../SYSTEMS.md).
- Source/build review cannot prove sensor accuracy, RF delivery, BLE pairing, GNSS fix quality, or safe flight behavior.

#### Conclusion

The reviewed RTOS implementation is internally consistent, statically provisioned, warning-clean in the stated scopes, and fail-closed at its software supervision boundaries after the corrections above. It is a strong integration baseline, not hardware or flight qualification. Review 6 separately audits documentation, onboarding, links, and repository hygiene.

## Review 6

### Review 6 — RTOS Documentation, Onboarding, and Repository Hygiene

#### Review identity

- **Review performed:** 2026-09-01
- **Scope:** collaborator entry path, RTOS/API contract, module guides, build/validation claims, function documentation, cross-links, source membership, third-party provenance, and excluded-artifact policy
- **Method:** fresh reader walkthrough from README to first control-hook integration; source-to-document comparison; terminology/number search; module-guide coverage matrix; local-link and Doxygen-tag automation; CMake/IAR parity check; and repository artifact scan
- **Result:** pass after the corrections recorded below

This is the third RTOS-specific author self-review and the sixth sequential repository review. It is not an independent reviewer or physical acceptance.

#### Reader questions applied

1. Can a new collaborator find the current status, safety boundary, build/test path, RTOS ownership rules, and first application integration point without relying on oral history?
2. Does each fitted sensor, communications module, and feedback output explain both its direct driver and the supported post-scheduler interface?
3. Are time domains, validity, freshness, maintenance, asynchronous completion, overflow, persistence, and peer/link limitations stated without ambiguity?
4. Do build sizes, toolchain status, test scope, and validation labels match the final evidence exactly?
5. Do all project functions carry the requested file header and Doxygen `@brief`, `@param`, and `@return` tags?
6. Can automation prevent raw KiCad sources, obsolete Atlas_Origins material, generated binaries, RTOS heap code, or build-membership drift from entering unnoticed?

#### Findings and corrections

| ID | Severity | Finding | Correction and disposition |
|---|---|---|---|
| R6-01 | Medium | `atlas_time.h` advertised an `AtlasTime_MillisecondsToTicks()` helper that did not exist. | Removed the phantom function from the major-functions header; the public timing surface now exactly matches the implementation. Closed. |
| R6-02 | Medium | Several current guides still described watchdog and service behavior as a “main loop” or generic foreground loop after RTOS integration. | Current BNO085, buzzer, status, and bring-up language now names `AtlasWatchdog` or `AtlasIO`. The historical Review 2 record retains its original wording with an explicit supersession note instead of being silently rewritten. Closed. |
| R6-03 | High | The BNO085 guide could lead an application author to compare the SH-2 report timestamp directly with the MCU HAL clock. | The RTOS section now requires the matching `bno_*_received_at_ms` field for MCU-clock age and treats the SH-2 timestamp/accuracy as separate report-quality metadata. Closed. |
| R6-04 | High | The BLE/RFD module pages described queued mode transitions but did not explicitly repeat the control inhibit across both maintenance and sensor recovery. | Both guides now require control inhibition for `maintenance_active` and `sensor_recovery_active`; the architecture document explains the ordered recovery-publication/heartbeat/busy-gate handoff. Closed pending bench stress. |
| R6-05 | Medium | Validation prose could be read as if the complete per-sensor stale-bit mapping were host-executed. | The ledger, project status, and build guide now distinguish host-tested wrap-safe age/supervisor decisions from the source-reviewed per-sensor required-bit/age mapping. Closed. |
| R6-06 | Medium | The review index and repository checker still covered only the original three communication reviews and did not enforce the RTOS integration contract. | Indexed all six passes. The checker now requires Reviews 4–6 and the RTOS files, verifies matching GNU/IAR kernel ports and every App source, parses/resolves the IAR project, and guards timing/ISR/static-allocation rules. Closed. |
| R6-07 | Low | Build-size evidence became stale after the final concurrency, idle-stack, and PPS-bound corrections. | Replaced all prior figures with clean final desktop-source values: 146,344-byte Debug flash, 74,064-byte Release flash, and 51,976-byte DTCM for both. Closed. |
| R6-08 | Low | The excluded-artifact check covered principal KiCad project extensions but not symbol, footprint, design-rule, job-set, library-table, or `.pretty` inputs. | Expanded the raw-KiCad extension/name/directory policy while continuing to allow reviewed exports and candidate manufacturing outputs. Closed. |

#### Documentation coverage

| Need | Authoritative location | Review result |
|---|---|---|
| First orientation and safety boundary | [Repository README](../../README.md) | Current RTOS capability and non-claims are visible before build instructions. |
| Coding, concurrency, review, and release rules | [Engineering standards](../DEVELOPMENT.md) | Static ownership, deadlines, freshness, interrupts, watchdog, evidence, and review gates are explicit. |
| Task graph and application API | [RTOS architecture](../reference/RTOS.md) | Tasks, priorities, rates, snapshots, commands, streams, maintenance, interrupts, memory, faults, and bench acceptance are complete. |
| Full firmware layering/failure model | [Firmware architecture](../reference/RTOS.md) | Generated/project/vendor boundaries and failure consequences agree with source. |
| Reproducible tool/test path | [Building](../DEVELOPMENT.md) | Tool versions, clean sizes, commands, output checks, IAR limitation, and troubleshooting are current. |
| Physical acceptance | [Bring-up](../SYSTEMS.md) | Safe setup, all modules, RTOS load/fault cases, and retained evidence template are present. |
| Claim vocabulary and matrix | [Validation status](../SYSTEMS.md) | Implemented/protocol-tested/target-built/bench-verified/flight-qualified remain distinct. |
| Current boundary and backlog | [Project status](../SYSTEMS.md) | Implemented, pending physical work, and deliberately absent control/safety functions are separated. |
| Dependencies and update discipline | [Third-party provenance](../reference/PROVENANCE.md) | CEVA and FreeRTOS source/version/license/update procedures are recorded. |

All ten device/output guides—ADXL375, LSM6DSV16B, MMC5983MA, MS5611, BNO085, GNSS, BLE, RFD900x, LED, and buzzer—contain a major-functions index, conservative validation state, explicit RTOS access/ownership section, board/protocol contract, health/fault interpretation, known limits, primary references, and repeatable bench acceptance. Direct examples are labeled pre-scheduler/isolated where post-start driver access would violate ownership.

#### Automated hygiene evidence

`Tests/repository/check_repository.ps1` passes for the reviewed tree and verifies:

- every local Markdown link resolves;
- all required entry, architecture, module-index, validation, RTOS, and six review documents exist;
- all project-owned App files have a Doxygen file/major-functions header;
- every recognized App function definition has an adjacent `@brief`, every parameter has `@param`, and non-void functions have `@return`;
- no raw KiCad source, Atlas_Origins path, build product, or FreeRTOS `heap_*.c` appears outside excluded build directories;
- no App runtime driver bypasses `AtlasTime_DelayMs()` with direct `HAL_Delay()`;
- no App/Core peripheral path calls an RTOS `...FromISR()` API without forcing a new priority review;
- all 17 App sources are in CMake and IAR;
- both builds contain the required common kernel sources, definition, include paths, and their matching Cortex-M7 r0p1 port, with no cross-selected port;
- the IAR XML parses and all 84 referenced file paths exist.

#### Residual limits

- The checker validates local Markdown targets but does not fetch or archive external manufacturer/FreeRTOS URLs.
- Documentation cannot validate electrical wiring, exact fitted part/firmware revision, real-time margins, or module communication. Those remain physical acceptance work.
- The records are sequential self-reviews, not the independent qualified approvals required by [Engineering standards](../DEVELOPMENT.md) for safety-relevant release.
- The project still has no selected project-level license; the README warns collaborators not to infer reuse rights.

#### Conclusion

A new collaborator now has a coherent, source-matched path from repository purpose and safety limits through RTOS application integration, module operation, build/test evidence, and physical acceptance. Automated checks cover the most likely documentation, source-membership, static-allocation, and artifact-policy regressions. The repository is ready for collaborative implementation work at the documented development maturity; it is not yet bench-verified or flight-qualified.

## Full review before corrections

The following is the complete pre-correction review published on 2026-09-01. Its failures and no-implementation statements describe that earlier snapshot, **not the current firmware**. Headings and links are rebased for this archive; the retired SD test-wrapper reference is marked. Current dispositions and the subsequent three correction-review passes are in [the current report](../REVIEW_REPORT.md).

## Baseline review: Complete codebase and documentation review

### Baseline review: Decision

**All-systems bring-up: not accepted.** The unchanged firmware cannot sustain an all-healthy startup and does not implement every requested board service. The documentation is consolidated around [Quick start](../QUICK_START.md), [Systems](../SYSTEMS.md), and [Development](../DEVELOPMENT.md); the findings below remain open unless explicitly marked as a documentation correction.

- Review date: **2026-09-01**.
- Firmware baseline: **`0dbc245` — Implementing RTOS**.
- This change edits documentation/repository checks and adds host-only diagnostic acceptance probes. It does **not** repair firmware, change hardware artifacts, flash a device, access a card, transmit RF, commission NVM, or actuate outputs.
- Owner clarification: J9's connector labeled “LoRa” will use **RFD900x**. The firmware radio target is correct; modem settings and peer operation are still unverified.
- These are **three sequential author self-reviews**, not three independent reviewers or a safety release approval. Earlier conclusions are retained only in the [historical archive](../archive/REVIEW_HISTORY.md).

### Baseline review: Scope and method

The review traversed all 17 project-owned App implementations/headers; generated startup, clocks, MSP, IRQs and fault paths; FatFs/BSP/disk configuration; USB initialization/CDC/PCD/descriptors; GNU/IAR source membership, startup and active memory layouts; existing host tests/mocks; repository checks; and all project-authored guides/reviews.

Vendor review covered the interfaces and selected implementation paths used to assess integration (notably HAL timer state, SD initialization/DMA, kernel tick/port behavior), provenance, and compiled build coverage. It was **not** a line-by-line audit or certification of the entire HAL, CMSIS, FreeRTOS, CEVA, FatFs or USB vendor implementation, nor a current vulnerability audit.

All ten schematic PDF pages were visually inspected, with focused checks of buses, USB, indicators, power and output paths. Manufacturing file hashes were checked, not fabrication correctness, PCB routing, BOM/CPL alignment, fitted substitutions or assembled electrical behavior. No physical board or external module was exercised.

### Baseline review: Three review passes

| Pass | Distinct work | Outcome |
|---|---|---|
| 1 — source, startup and hardware contracts | Trace boot → driver → HAL; all peripheral call sites; bus/IRQ ownership; schematic power/pin cross-check; installed capability vs active service | Found shared TIM2 startup failure, SD/USB/service gaps, DMA placement conflicts and incorrect hardware/LED assumptions |
| 2 — reproducible behavior and build evidence | Clean GNU Debug/Release; existing host suite; strict target analysis; linker/symbol/stack inspection; new positive-control + adverse-case probes | Builds/protocol tests pass while six integration findings reproduce; analyzer and mock limits recorded rather than promoted to hardware success |
| 3 — documentation, tests and preservation | Consolidated reader walkthrough; module/API/claim cross-check; probe review; local links/anchors and artifact/membership checks; compare firmware/hardware against baseline | Current entry path and finding status made explicit; historical pass claims superseded; final check results recorded below |

### Baseline review: Priority summary

P1 means a bring-up/operational blocker or serious latent integration risk. P2 means a resilience/real-time guarantee gap that must be resolved before relying on that behavior. “Latent” means the service is not currently started; it is not a defect observed on physical hardware.

| ID | Priority | Finding | State |
|---|---|---|---|
| [R01](#baseline-review-r01-gnss-startup-restarts-an-already-running-timer) | P1 | GNSS restarts TIM2 already started by BNO085 | Reproduced; firmware unchanged |
| [R02](#baseline-review-r02-rtos-waits-do-not-guarantee-minimum-device-delays) | P1 | Tick waits can undershoot physical minimum delays | Reproduced at timing boundary; firmware unchanged |
| [R03](#baseline-review-r03-sd-lazy-initialization-uses-an-unconfigured-handle) | P1 | SD lazy initialization passes an unconfigured HAL handle | Reproduced; latent until mount/init |
| [R04](#baseline-review-r04-sd-read-completion-can-be-erased) | P1 | SD read completion flag reset occurs after DMA launch | Reproduced; latent |
| [R05](#baseline-review-r05-sd-timeouts-do-not-establish-buffer-release) | P1 | SD timeout lacks guaranteed DMA quiescence and bounded task-friendly completion | Reproduced at adapter boundary; latent |
| [R06](#baseline-review-r06-default-memory-is-not-sdmmc1-or-dma1-accessible) | P1 | Default DTCM layout is invalid for direct SDMMC1/DMA1 buffers | Source/map/manual confirmed; latent |
| [R07](#baseline-review-r07-usb-is-not-a-complete-or-started-cdc-service) | P1 | USB not started; RX/control/TX lifecycle incomplete | Source confirmed; latent |
| [R08](#baseline-review-r08-gnss-parser-has-no-truncated-frame-age-limit) | P2 | Truncated frame can suppress later valid GNSS data | Reproduced; remains after R01 is fixed |
| [R09](#baseline-review-r09-hardware-labels-and-power-protection-claims) | P1 | Incorrect pyro protection claim and conflicting LED color mapping | Documentation corrected; physical mapping pending |
| [R10](#baseline-review-r10-deadline-checks-do-not-cover-release-lateness) | P2 | Timing checks do not prove scheduled response deadlines | Source confirmed; no physical timing acceptance |
| [R11](#baseline-review-r11-configured-peripherals-are-not-complete-services) | P1 | Missing services/commissioning prevent all-systems operation | Capability gap; no implementation added |

### Baseline review: R01: GNSS startup restarts an already-running timer

Sources: [board startup](../../App/Src/atlas_board.c), [BNO085 init](../../App/Src/atlas_bno085.c), [GNSS init](../../App/Src/atlas_gnss.c), [bundled HAL timer](../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c).

`AtlasBoard_Init()` passes the same TIM2 handle to BNO085 and then GNSS. BNO085 calls `HAL_TIM_Base_Start()`; the HAL changes the handle state from READY to BUSY. GNSS calls that function again, but this HAL rejects any state other than READY. GNSS returns `ATLAS_ERROR_IO` before its MON-VER poll and the RAM configuration is not ready.

The new stateful timer probe passes isolated GNSS startup, then reproduces the shared-running-timer failure with **zero MON-VER polls**. The old host stub always returned HAL_OK and did not run board/BNO startup, masking the conflict. The startup aggregate then prevents watchdog refresh; a sustained healthy system is not possible on this path.

Required correction: establish one owner/start point for TIM2 while preserving BNO085 time and GNSS capture. Do not stop/reset the shared clock merely to make the second call succeed. Acceptance needs actual board-order startup, one timer start, advancing BNO timestamps, GNSS identity/configuration and PPS.

### Baseline review: R02: RTOS waits do not guarantee minimum device delays

Sources: [time helper](../../App/Src/atlas_time.c), [MS5611 conversion](../../App/Src/atlas_ms5611.c), [BLE transition](../../App/Src/atlas_ble.c). Primary references: [FreeRTOS tick resolution](https://www.freertos.org/Documentation/02-Kernel/05-RTOS-implementation-tutorial/02-Building-blocks/11-Tick-Resolution), [TE MS5611 datasheet](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocFormat=pdf&DocLang=English&DocNm=MS5611-01BA03&DocType=Data+Sheet&PartCntxt=MS561101BA03-50), [u-blox data-mode timing, section 5.1.4](https://content.u-blox.com/sites/default/files/u-connectXpress-ATCommands-Manual_UBX-14044127.pdf?hash=undefined).

The running-scheduler branch converts milliseconds to ticks and calls `vTaskDelay()` without accounting for phase within the current tick. At 1 kHz, N ticks can wait only just over N−1 milliseconds. The header's “at least” promise is false.

For OSR1024, MS5611 requests 3 ms while the conversion maximum is 2.28 ms. The probe models a legal 2.001 ms wake; actual bus/CPU overhead is not a guaranteed replacement for the missing delay. The datasheet warns that early ADC access can return zero and disturb conversion. Other OSR waits also need correction. BLE requests a minimum 50 ms ATO1-to-payload gap but can obtain 49.001 ms; explicit MMC5983MA SET/RESET waits and other post-start minimum delays need the same audit.

Required correction: a minimum-elapsed-time contract with overflow-safe conversion and scheduler-phase allowance, followed by protocol-specific timing tests. Verify physical minimum delay separately from maximum I/O latency. This probe is a legal scheduling model, not a measured target waveform or a claim that every conversion fails.

### Baseline review: R03: SD lazy initialization uses an unconfigured handle

Sources: [main](../../Core/Src/main.c), [FatFs registration](../../FATFS/App/fatfs.c), [BSP](../../FATFS/Target/bsp_driver_sd.c), [HAL SD](../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c).

`main()` calls `MX_FATFS_Init()` but never `MX_SDMMC1_SD_Init()`, the helper that assigns `hsd1.Instance` and its configuration. FatFs initialization only links the disk driver. On a later mount, `BSP_SD_Init()` calls `HAL_SD_Init(&hsd1)` while that global handle is still unconfigured.

The mock observes a NULL peripheral instance at the HAL boundary. The real HAL assumes a valid instance and can reach invalid register access; its disabled parameter assertions are not a recovery path. The source comment promising valid lazy initialization is therefore misleading.

Required correction: define the complete SDMMC1 configuration/card lifecycle before FatFs can initialize it, without making absent media fatal to unrelated startup. A successful `f_mount()` path must be tested; adding the mount alone is not a fix.

### Baseline review: R04: SD read completion can be erased

Source: `SD_read()` in [sd_diskio.c](../../FATFS/Target/sd_diskio.c).

The adapter starts DMA, then clears `ReadStatus`. If completion interrupts after DMA is enabled but before that store, the callback's completion is erased and the reader waits until timeout. The write path correctly clears its flag before launching.

The probe delivers completion at the launch boundary to model that allowed interleaving: immediate read completion returns `RES_ERROR` after about 30 seconds of modeled time, while delayed read and immediate write controls pass.

Required correction: initialize completion state before launch and use an operation-scoped completion protocol that also handles error, cancellation, stale callbacks and serialization.

### Baseline review: R05: SD timeouts do not establish buffer release

Sources: [disk adapter](../../FATFS/Target/sd_diskio.c), [BSP callbacks](../../FATFS/Target/bsp_driver_sd.c).

Read/write timeout paths return without requesting/verifying abort or otherwise proving DMA has released the caller's buffer. In the stalled-transfer model, the function returns an error while DMA still owns it. This does not claim every HAL error leaves DMA active; it shows the adapter has no guaranteed ownership handoff on its timeout path.

It also spins on completion/card state, with 30-second timeout phases and no task-friendly wait. Moving that code into `AtlasIO` could starve lower-priority control and prevent required progress. Error/abort callbacks do not supply a complete wake/result path.

Required correction: one storage owner; bounded IRQ-to-task completion; explicit success/error/abort states; verified quiescence before buffer reuse; media-removal handling and stale-callback tests. FatFs `_FS_REENTRANT=0`; `_FS_LOCK=2` is not an RTOS mutex.

### Baseline review: R06: Default memory is not SDMMC1 or DMA1 accessible

Sources: [GNU linker](../../STM32H743XX_FLASH.ld), [active IAR linker](../../EWARM/stm32h743xx_flash.icf), [disk adapter](../../FATFS/Target/sd_diskio.c). Primary references: [ST AN4891, DTCM access](https://www.st.com/resource/en/application_note/dm00306681.pdf), [ST AN5200, SDMMC1 access table](https://www.st.com/resource/en/application_note/an5200-getting-started-with-stm32h7-mcus-sdmmc-host-controller-stmicroelectronics.pdf).

Ordinary writable data and static task stacks are in DTCM at `0x20000000`; current target maps allocate no ordinary data in the other SRAM regions. DTCM is directly accessible to CPU/MDMA, not SDMMC1 IDMA or DMA1. No reachable SD bounce buffer, explicit DMA section, or MDMA bridge is implemented. The adapter directly casts caller buffers; its optional scratch-buffer path is disabled.

Required correction: select memory reachable by the specific DMA master (SDMMC1's supported AXI path, not an arbitrary “SRAM” region), preserve alignment/ownership, update both linkers and startup initialization as necessary, and validate addresses in maps plus actual transfers. ADC acquisition using DMA1 needs its own reachable buffers. **D-cache is not enabled today**; cache coherency is an additional future design obligation, not the active cause reported here.

### Baseline review: R07: USB is not a complete or started CDC service

Sources: [USB device](../../USB_DEVICE/App/usb_device.c), [CDC interface](../../USB_DEVICE/App/usbd_cdc_if.c), [PCD setup](../../USB_DEVICE/Target/usbd_conf.c), [GPIO callback routing](../../App/Src/atlas_board.c).

`MX_USB_DEVICE_Init()` has no caller, so ordinary boot does not enumerate. Merely adding that call would still leave `CDC_Receive_FS()` discarding incoming bytes, control requests (including GET_LINE_CODING) unimplemented, and no task-owned command/console service.

`CDC_Transmit_FS()` dereferences `pClassData` without a configured-class check; before enumeration or after teardown it can be NULL. It also hands the caller's buffer to an asynchronous transfer without a project-level lifetime/serialization contract.

PCD VBUS sensing is disabled. PA9's external VBUS divider is configured as an input/EXTI, but application connection gating is absent. The externally powered design needs a reviewed connect/disconnect policy, not unconditional pull-up exposure. The schematic's D−/D+ routing was rechecked and **is not reported as swapped**.

Required correction: complete the initialization/VBUS lifecycle, valid CDC control behavior, bounded RX queue and task processing, configured-state/TX-busy checks, retained TX ownership and disconnect recovery. USB behavior was source-reviewed, not host- or hardware-executed in this review.

### Baseline review: R08: GNSS parser has no truncated-frame age limit

Source: the length/discard states in [atlas_gnss.c](../../App/Src/atlas_gnss.c).

An oversized UBX length enters discard mode for the entire declared payload plus checksum. There is no inter-byte/frame timeout. A header declaring 65535 bytes followed by a long gap and a valid NAV-PVT can leave that valid frame treated as discarded payload.

The probe waits 5 seconds of modeled time and injects a valid NAV-PVT after the truncated header. No navigation sample is published and `discard_remaining` is still 65437. Buffer writes are bounded; this is a recovery/availability defect, not a demonstrated memory overflow.

Required correction: a bounded resynchronization policy for truncation, overflow and prolonged gaps, with fragmented valid frames, noise, embedded sync bytes and tick-wrap tests. R01 currently masks normal operation; R08 remains after startup is corrected.

### Baseline review: R09: Hardware labels and power-protection claims

Source: [schematic PDF](../../hardware/Atlas-schematic-rev-0.1.pdf); consolidated [hardware evidence](../reference/HARDWARE.md).

1. **Pyro feed:** J5 links `VIN_RAW` to `PYRO_VBAT_ARMED`, bypassing TPS16850 U11. The former hardware overview incorrectly included “shared input protection” among constraints on pyro current. That claim is removed; no independent channel limiter is shown. The physical arm link remains open for ordinary development.
2. **LED mapping:** software `LED_G`/PB7 reaches D5 pin 4 (B), and `LED_B`/PD14 reaches pin 3 (G), according to the schematic. Guides now distinguish logical masks from emitted colors. Exact fitted-part/as-built verification is required before remapping firmware or interpreting colors.
3. **Radio label:** the owner explicitly confirmed RFD900x on J9. The legacy “LoRa” label is now explained, not treated as a protocol-selection defect.

Disposition: incorrect documentation is corrected in this change. Neither the schematic nor hardware nor firmware was altered, and the LED discrepancy is not claimed physically resolved.

### Baseline review: R10: Deadline checks do not cover release lateness

Source: `atlas_rtos_application_task()`, `atlas_rtos_io_task()` and supervisor policy in [RTOS implementation](../../App/Src/atlas_rtos.c).

The application measures elapsed time starting **after** `vTaskDelayUntil()` returns. It catches a slow snapshot/hook cycle, but not a late task release caused by higher-priority I/O/interrupts. A task can start well after its scheduled deadline, finish quickly, and still advance its heartbeat within the 100 ms supervisor interval. For example, stream commands permit a 50 ms synchronous UART timeout in priority-4 `AtlasIO`; a blocked BLE transmit can delay priority-3 control without that wait being included in the control cycle's elapsed-time check.

`io_deadline_misses` counts cycles exceeding 20 ms without independently latching a fault. Synchronous sensor conversions/maintenance can also reduce delivery rates; device ODRs are not lossless acquisition guarantees.

Required correction: define and measure release jitter, complete response deadlines, skipped-release/catch-up policy, queue latency and acceptable sensor sample loss. Decide which overruns are faults. Current documentation now calls the 100 Hz cadence nominal, not proven hard-real-time operation.

### Baseline review: R11: Configured peripherals are not complete services

Sources: [main](../../Core/Src/main.c), [RTOS command/snapshot API](../../App/Inc/atlas_rtos.h), [FatFs configuration](../../FATFS/Target/ffconf.h), and per-system sources linked in [Systems](../SYSTEMS.md).

- **SD:** no mount/open/write/logger, removal lifecycle or valid file-time service; card detection always reports present.
- **BLE/RFD900x:** identity/transport or configuration helpers are not a commissioned peer link. BLE ordinary boot does not activate the complete SPS path. General configuration is not exposed in the RTOS queue; persistent changes must remain deliberate. No project framing/security/reconnect layer exists.
- **ADC:** generated ten-channel ADC1 and two-channel ADC3 setup is not calibration, conversion start, sampled data, scaled rail/continuity telemetry or thresholds.
- **PWM/GPIO/expansion:** timers/pins/UART/I2C/SPI are configured, but actuator channels are not started and no complete task-owned control/client interface exists.
- **Pyro:** five low gate outputs only. No physical-arm interpretation, valid continuity acquisition, arming/firing state machine, independent pulse limit or qualified fault/lockout behavior.
- **RTC/power/ECC:** initialization and IRQ scaffolding do not implement wall-clock/file-time management, complete power/RAM fault monitoring, or retained reset/crash records.
- **Application:** the weak control hook is empty; no flight algorithm, logger, telemetry protocol or comprehensive output-safe-state controller exists.

These are explicit scope/capability gaps, not newly introduced regressions. The [readiness matrix and acceptance gates](../SYSTEMS.md) now make each visible. Implementing them requires separately reviewed service contracts and inert tests; this review did not add hazardous output behavior.

### Baseline review: Executable evidence

Tests used Arm GNU 14.3.1 (14.174), CMake 3.26.4 with MinGW Makefiles/GNU Make, host GCC 13.1.0 and PowerShell 7. The Ninja presets were inspected but not used for the fresh target builds; no IAR compiler/hardware was available.

| Check | Observed result | Boundary |
|---|---|---|
| Fresh complete GNU Debug | Pass; **146540 bytes flash, 51976 bytes DTCM** | Normal repository `-Wall` build, no compiler warnings |
| Fresh complete GNU Release | Pass; **74264 bytes flash, 51976 bytes DTCM** | Normal repository `-Wall` build, no compiler warnings |
| Strict target compile/analyzer, 17 App sources | Pass: `-O0 -Wall -Wextra -Werror -fanalyzer` | Actual object compilation, not syntax-only analysis |
| Main/IRQ strict analyzer | Reports deliberate terminal infinite loops | Not included in the zero-finding App claim; rerun allowing only that diagnostic to remain a warning |
| Existing host protocol suite | Pass, 15 test functions | Selected contracts under mocks, not full board/RTOS execution |
| Focused review probes | **Fail, exit 1; three groups / six finding IDs** | R01/R02/R03/R04/R05/R08 reproduced under boundary models |
| Kernel handlers / allocator symbols | One each of SVC, PendSV, SysTick and port tick; no linked malloc/sbrk/FreeRTOS heap allocator | GNU Debug symbol inspection; not runtime stack proof |
| Static stack frames | Largest reviewed App frame 696 bytes (supervisor), then 632 (application task), 568 (GNSS send) | Individual frames, not summed call-chain/ISR/FPU worst case |
| IAR | XML/member/port/linker inspection only | No binary or IAR size/timing claim |
| Vendor provenance | 31 kernel files match after newline normalization; 12 CEVA files byte-identical | Clean pinned CEVA checkout and installed CubeH7 package; no security certification |
| Manufacturing manifest | All 19 file hashes match | Preservation, not fabrication acceptance |
| Physical operation | **Not tested** | No sensor, radio, USB, SD, ADC, actuator or pyro bench claim |

Build sizes include current path-dependent strings such as assertion file names and are evidence for this snapshot/build environment, not immutable limits. Remaining SRAM regions are not used by ordinary data in these maps. Historical size values are retained only in the archive.

#### Baseline review: Reproduce the behavioral findings

From the repository root with host GCC on PATH:

```powershell
pwsh -NoProfile -File Tests/review/run_review_probes.ps1
```

Representative observed output:

```text
CONTROL PASS: zero and pre-scheduler timing
FAIL R02: requested 3 ms, earliest modeled wait 2001 us
FAIL R02: requested 50 ms, earliest modeled wait 49001 us
CONTROL PASS: isolated GNSS startup and clean NAV-PVT
FAIL R08: after 5 s and a valid NAV-PVT, discard_remaining=65437
FAIL R01: GNSS with shared running timer returned IO; MON-VER polls=0
FAIL R03: BSP passed an unconfigured SD peripheral handle to HAL_SD_Init
CONTROL PASS: delayed read completion and immediate write completion
FAIL R04: early read completion returned 1 after 30003 modeled ms
FAIL R05: timeout returned 1 with mock DMA still owning the caller buffer
REVIEW GAPS OPEN: 3 probe groups failed acceptance.
```

The [timing probe](../../Tests/review/test_timing.c) executes the real RTOS-aware helper against an earliest-legal-wake model. The [GNSS probe](../../Tests/review/test_gnss_integration.c) executes real GNSS/UART code with a stateful HAL timer stub and injected replies; it models BNO's already-started timer, not a complete BNO/board run. The [SD probe](../../Tests/review/test_sd_integration.c) executes real disk/BSP adapters with controlled HAL completion and ownership. Its then-current `Tests/review/sd_bsp_host.c` wrapper (retired after this baseline; recoverable from the pre-correction backup) adapted weak callback linkage for MinGW; callback injection is at the BSP/disk boundary, not a HAL IRQ-routing test.

Positive controls must continue passing. Exit 2 denotes a harness/build failure, not a reproduced firmware defect. The SD group suppresses pre-existing generated unused-parameter warnings only. Passing these probes after a future fix would still not prove electrical communication, real DMA reachability, full FreeRTOS behavior, or complete protocol resilience.

The R03 fixture deliberately models the current uninitialized `hsd1` at the BSP boundary. If a future fix moves ownership into board startup instead of the BSP, update the fixture only alongside a test proving that real startup prepares the handle before filesystem use. Do not weaken a test merely to remove its failure message.

#### Baseline review: Final documentation and preservation checks

All three review passes are complete. The following checks were performed on the consolidated work, with the host/probe/repository commands repeated after publication into the actual desktop repository:

| Check | Result |
|---|---|
| Reader path and guide count | One documentation hub; Quick start + Systems form the starting path. Project-authored guides reduced from **31 to 22**, retaining ten optional device references and one historical archive; vendor notices and the PR template are excluded from this count. |
| Repository checker | **Pass: 254 local links and 48 heading anchors**, required guides, App Doxygen tags, artifact policy and GNU/IAR membership. This is the documented Markdown subset, not a general parser or external-link check. |
| Checker negative controls | Exactly two intended failures for a missing file and heading; a duplicate-heading suffix resolves and nonexistent links inside both supported fence styles are ignored. The temporary fixture was removed. |
| Documentation/source consistency | 69 documented Atlas function identifiers matched source/header text. The complete non-actuating Development example target-compiled with C11, `-Wall -Wextra -Werror`; name matching alone is not API execution coverage. |
| Published host/probe rerun | Existing host suite passes; review probes still return **exit 1** for R01/R02/R03/R04/R05/R08, with positive controls passing. A missing-compiler harness control correctly returns **exit 2**. |
| Publication preservation | All **311 baseline files outside the planned documentation/checker edits are byte-identical** to their pre-publication checkout SHA-256 values, including firmware, existing host tests, vendor code, build definitions, linkers and hardware exports. |
| Retired paths and recovery | 18 obsolete/duplicate guide paths were retired and ten device guides relocated. Six earlier reviews remain in the historical archive; the complete originals are recoverable from `0dbc245` and an independently retained baseline ZIP. |

The staging copy came from Git's archive and differs from the checkout in line endings for some unchanged files. That normalization was checked explicitly; those firmware/build/vendor files were **not copied back**. Only the scoped documentation, repository checker, PR template, hardware README links and new host-only review probes were published. No generated build products, raw KiCad designs or obsolete origin material were added.

### Baseline review: Recommended closure order

1. Fix R01/R02 with real shared-resource/timing contracts and keep the new acceptance probes red until behavior actually meets their assertions.
2. Fix GNSS resynchronization and establish release-deadline/sampling requirements (R08/R10); bench-accept sensors, clocks and RTOS under concurrent traffic.
3. Implement SD as a complete owned service, addressing R03–R06 before allowing a filesystem caller; implement USB lifecycle and transport (R07).
4. Commission the exact RFD900x/BLE modules deliberately, then add framing, security, reconnect and load tests.
5. Add ADC, GPIO, expansion and actuator services under explicit contracts. Pyro functionality requires a separate qualified safety design and approval, not a bring-up shortcut.
6. Retain physical evidence for every [system gate](../SYSTEMS.md#acceptance-gates), independent safety reviews where required, and an explicit release decision.

Finding closure must record the correcting revision, reproduction-before/fix-after evidence, affected builds, fault tests, physical measurements where needed, remaining limits and reviewer approval. A documentation edit or passing compile alone cannot close a firmware/hardware finding.
