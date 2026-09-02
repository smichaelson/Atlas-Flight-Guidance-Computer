# Atlas software review record

Latest update: 2026-09-02. The [PCB bring-up review](#pcb-bring-up-review) below covers the new diagnostic image, dashboard and startup procedure. The subsequent correction-review sections retain the preceding full-codebase findings and their evidence; their earlier build hashes/counts are historical, not the latest artifacts. All source changes stay within the repository; the separate original Atlas folder is untouched.

**Software implementation and offline regression coverage are in place. Physical acceptance and flight release remain unapproved.** The Bringup image permanently inhibits PWM/pyro; normal output configuration remains unqualified/disabled by default. Three additional author review passes cover the bring-up work. They are not independent safety approvals.

The [pre-correction full review](archive/REVIEW_HISTORY.md#full-review-before-corrections) preserves the original findings, failing probes, build figures and documentation-consolidation evidence. Use [Systems](SYSTEMS.md) for current subsystem readiness and [Peripheral services](PERIPHERALS.md) for API contracts.

## PCB bring-up review

The operator's single procedure is **[startup](startup.md)**. The diagnostic scheduler is deliberately separate from the normal application: no control hook, actuator configuration, PWM enable or pyro arming/firing. ADC/input/USB services start immediately; sensor/link probes and SD mounting/testing require explicit commands. Component absence is visible without invoking the normal flight sensor-freshness reset loop. GPIO tests are bounded logic-only requests, not actuator tests.

### Three additional review passes

| Pass | Review performed | Result |
|---|---|---|
| 1 — hardware and functional contracts | Traced boot/power/USB/connectors against schematic and manufacturing pad attributes; reviewed staged ownership, output denial, command allowlist, measurement units and removable-media behavior | Complete. Recorded the USB polarity/protection conflict and provisional power gates; corrected result-backpressure and reconnect/identification handling |
| 2 — implementation and adverse cases | Built both profiles in Debug/Release, inspected symbols/memory, ran target compiler analysis and injected protocol, output and storage failures | Complete. Tests cover duplicate IDs, loss/overflow, old sessions, blocked/stale clients, GPIO expiry/disconnect, exclusive SD create/readback and failures; first diagnostic watchdog fault is retained |
| 3 — operator walkthrough and preservation | Read the complete startup path against code/pins; cross-checked exact payloads, artifacts and dashboard labels; reran final builds/tests/analysis and compared the preserved checkout | Complete. Corrected 23-byte SD / 19-byte link fixture descriptions, fixed signed numeric edge/null display handling, and made UI disconnect recover even if serial close fails |

These are three separate, sequential **author** reviews, not three independent reviewers. No actual PCB, COM port, DFU session, RF peer, motor or energetic load was accessed. The synthetic dashboard and modeled serial lifecycle do not establish Windows-driver/electrical interoperability.

### Latest executable evidence

Toolchain: Arm GNU 14.3.1, CMake 3.26.4, MinGW Makefiles/GNU Make, host GCC 13.1.0, PowerShell 7 and Python 3.12.13/Tk. The extension's `cube-cmake` environment and Ninja were not used for the successful builds; [startup](startup.md#build-the-correct-image) includes the tested Makefiles fallback. No IAR build was performed.

| Check | Final result | Evidence boundary |
|---|---|---|
| Four clean target builds | Pass, no compiler warnings | Complete diagnostic and normal linked GNU images; matrix below |
| Strict target compiler/analyzer | Pass, **40 translation units per profile** | 25 App sources, 11 project adapters and 4 USB middleware files compiled to real objects with `-O0 -Wall -Wextra -Werror -fanalyzer`; not syntax-only |
| Existing host, focused-review and service suites | Pass | Includes real GNSS/SD/FatFs/USB paths and inert output policy/adapter models; no physical timing claim |
| [Bring-up suite](../Tests/bringup/run_bringup_tests.ps1) | Pass | Four C test executables, **14 Python tests**, actual C console JSON decoded by the desktop code, and a hidden 14-row Tk smoke test |
| Framing/formatting | Pass | 100,000 deterministic fuzz bytes, overflow/loss recovery, finite allowlist, signed boundaries/nonfinite values, escaped identities and bounded output |
| Desktop lifecycle and artifacts | Pass | Inert open-with-DTR-low ordering, no automatic TX/retry, partial-write uncertainty, failed-close recovery, stale/reset handling, SD fixture preservation and wrong-image/vector/hash rejection |
| Repository/documentation checks | Pass | Required files, local links/heading anchors, App Doxygen tags, static-memory/build membership and artifact policy; see reproducible commands in startup |
| Linked profile boundary | Pass | Diagnostic entry present; normal scheduler/control hook absent; `AtlasRtos_OutputsPermitted()` returns zero; tested command boundary separately denies PWM/pyro/configuration |

The compiler audit suppresses only the intentional infinite-loop diagnostic in `main.c` and `stm32h7xx_it.c`, after inspection of their fatal inhibit paths, as in the preceding audit. No other analyzer warning is waived. Tests using inert register/serial/filesystem boundaries state those limits in their sources; they are not complete kernel/device simulations.

| Configuration | FLASH bytes | DTCM bytes | AXI DMA bytes |
|---|---:|---:|---:|
| Bringup / Debug | 206640 | 121384 | 64 |
| BringupRelease / Release | 107880 | 121288 | 64 |
| Normal Debug | 202008 | 89840 | 64 |
| Normal Release | 104080 | 89816 | 64 |

DTCM totals include the 16 KiB main-stack reservation. Diagnostic `_ebss` is `0x20019A28` in Debug and `0x200199C8` in Release: only **9432 / 9528 bytes** remain before the MPU guard at `0x2001BF00`. This is not space available for unreviewed new buffers. Main stack remains `0x2001C000`–`0x20020000`; both 32-byte DMA blocks remain in AXI SRAM. Every image has one strong SVC/PendSV/SysTick/ADC/DMA1-stream-0/1/OTG-FS handler and no linked allocator/printf entry points. Stack-frame inspection and high-water telemetry do not replace full call-chain/FPU/interrupt and physical load testing.

Both diagnostic HEX/BIN/ELF manifests pass the actual offline verifier, including target/profile, hashes, initial SP/reset vectors, checksummed HEX and bank-1 boundaries. Exact final HEX SHA-256 values from this checkout:

```text
Bringup Debug   f265281eb0628f547627efe940fd065ef26e7268a075c5673e036eed888234b9
Bringup Release e4a9af7d8a43518b3f8ae08fcb87483c2780178f9964c8fe9b0211a6b3652507
```

Build artifacts are retained outside the source repository. Rebuild from your checkout, verify its generated manifest, and retain that image's evidence; source paths/compiler differences can alter the binary/hash. This is an accidental-wrong-image/corruption check, not signed firmware authentication.

### Physical gates and preservation

The most important unresolved bring-up finding is **USB data polarity**: the PDF's J2/U16 connections conflict with the expected exported copper pad assignments. Neither source proves the assembled board. Perform the [unpowered D+→PA12 / D−→PA11 check](startup.md#usb-data-polarity-is-a-required-as-built-check) and review U16's VBUS support-pin wiring before USB commissioning. No PCB/netlist/manufacturing file was changed to hide the discrepancy. The eFuse input threshold, provisional current limits, crystal/boot options and actual rail/temperature/ripple behavior also require the explicit physical gates in startup.

No result here verifies sensor calibration/axes, SD-card durability, GNSS navigation/PPS quality, BLE SPS peer compatibility or RF range. The diagnostic image intentionally cannot qualify active PWM/pyro operation; their separate inert acceptance plan and approved normal configuration remain required. No software claim replaces disconnection of those loads.

Preservation audit against the pre-bring-up ZIP: **all 374 existing repository files retained**, with 348 byte-identical and 26 intentionally changed; 18 files added. All 459 archived Git-internal files and all vendor sources are byte-identical. All **19 manufacturing SHA-256 entries** pass. No existing file was deleted in this bring-up work, and nothing was staged/committed. The original Atlas folder was not touched. Generated Python caches and external test/build artifacts are excluded from source-file counts.

The complete pre-bring-up checkout, including existing dirty changes and `.git`, remains recoverable from the external `atlas_before_bringup_20260902.zip`, SHA-256:

```text
FD14CEBA8B3E6E94268320B3FEC8F04E563771AFC37873D765ADCF831EA843ED
```

The remaining sections record the **preceding correction review** and its original measurements; the latest build matrix above supersedes its build figures for the present checkout.

## Scope and method

Review covers all project-owned App implementations/headers and changed startup, clocks, MSP/IRQ/fault adapters, SD/FatFs, USB core/class/PCD, GNU/IAR membership/linker integration, tests and current documentation. Vendor review is limited to relevant integration paths and preserved provenance—not a line-by-line certification or current vulnerability audit of every dependency.

The schematic's ten pages were inspected in the preceding review, with additional focused power/output/ADC/connector cross-checks during correction. No board, module, card, RF peer or energetic load was exercised. No flashing, RF transmission, persistent module commissioning, filesystem formatting or physical firing was performed.

Owner-confirmed requirements: RFD900x on J9; KST X10 V8.0 servos; nominal 500 ms pyro pulse; ongoing continuity dependent on physical J5 connection; explicit software arming; at most three retries; at least 500 ms OFF. The implementation conservatively caps **four attempts per channel per boot**, preserving the budget across disarm/rearm and manual requests.

## Three correction-review passes

| Pass | Scope | Status |
|---|---|---|
| 1 — functional and fault contracts | Startup/shared time, SD lifecycle, USB control/retention, output timer/DMA/ADC/queues, commissioning and expansion; focused positive/adverse model tests | Complete; corrected additional implicit SD remount, short USB control/status lifetime, output-OFF queue fencing and ECC duplicate-count issues |
| 2 — separate rerun by the author | Clean Debug/Release, strict target compiler/analyzer, map/stack/handler inspection, full suite reruns; cross-check newly centralized API documentation | Complete; also corrected IAR stack placement, missing GNU guard exclusion and unintended libc allocation paths |
| 3 — delivery and preservation | New-collaborator walkthrough, examples/links/checker negative controls, regression review, exact target-drift/preservation and final target reruns | Complete; published sources match staging, unchanged files are byte-identical, and actual-checkout builds/tests/analysis pass |

All three passes are separate sequential author reviews, **not reviews by other people**. Two qualified independent safety reviewers and inert physical acceptance remain release prerequisites under [Development](DEVELOPMENT.md#change-and-release-checklist).

## Finding disposition

“Corrected in software” means the identified code/ownership defect is addressed and relevant tests pass; it does not close its physical acceptance gate.

| ID | Disposition |
|---|---|
| [R01](#r01-gnss-startup-restarts-an-already-running-timer) | Corrected: idempotent shared TIM2 start preserves counter/capture |
| [R02](#r02-rtos-waits-do-not-guarantee-minimum-device-delays) | Corrected: minimum elapsed waits with phase/overflow/context handling |
| [R03](#r03-sd-lazy-initialization-uses-an-unconfigured-handle) | Corrected: full SDMMC configuration and explicit optional-media lifecycle |
| [R04](#r04-sd-read-completion-can-be-erased) | Removed by design: synchronous polling port has no SD DMA completion race |
| [R05](#r05-sd-timeouts-do-not-establish-buffer-release) | Corrected: no retained SD DMA ownership; bounded polling/reset and media-generation invalidation |
| [R06](#r06-default-memory-is-not-sdmmc1-or-dma1-accessible) | Corrected: CPU-only SD/USB; private AXI ADC/pyro DMA blocks in both linkers, ECC seeding |
| [R07](#r07-usb-is-not-a-complete-or-started-cdc-service) | Implemented/corrected: VBUS owner, CDC controls, retained RX/TX, reset/timeout handling and FIFO allocation |
| [R08](#r08-gnss-parser-has-no-truncated-frame-age-limit) | Corrected: bounded UBX frame/interbyte age and transport-loss resynchronization |
| [R09](#r09-hardware-labels-and-power-protection-claims) | Documentation/radio assignment corrected; physical LED mapping remains unresolved |
| [R10](#r10-deadline-checks-do-not-cover-release-lateness) | Corrected deadline coverage, priorities and fault gates; physical schedulability remains unverified |
| [R11](#r11-configured-peripherals-are-not-complete-services) | Added owned storage/RTC, USB, ADC/GPIO/PWM/pyro, expansion and commissioning APIs; qualified output profile and physical acceptance still required |

## R01: GNSS startup restarts an already-running timer

[AtlasTime_StartCounter](../App/Src/atlas_time.c) accepts only READY/stopped or BUSY/running states. BNO085 and GNSS both use it; a second client no longer calls HAL start again or resets the shared counter. A stopped BUSY handle still fails closed.

The real GNSS/UART probe models BNO's already-running timer, proves one HAL start, preserves the counter and proceeds to MON-VER/configuration. This is not a complete BNO/board startup simulation; actual BNO time, GNSS readback and PPS remain bench gates.

## R02: RTOS waits do not guarantee minimum device delays

[The helper](../App/Src/atlas_time.c) uses upward rounding plus one tick-phase allowance, overflow-safe chunks and explicit context rules. Pre-scheduler HAL waits are chunked; nonzero ISR/suspended-scheduler use asserts. The [timing tests](../Tests/review/test_timing.c) cover zero, maximum integer duration, earliest legal wake, pre-scheduler and context failures at modeled 100, 1000 and 2000 Hz.

At 1 kHz, the tested requested 3 ms wait has a modeled minimum of 3001 us, replacing the former 2001 us failure. This fixes the logical minimum-wait contract used by MS5611, MMC5983MA and BLE. Physical command timing and full-system 1 kHz scheduling still require measurement.

## R03: SD lazy initialization uses an unconfigured handle

[The BSP](../FATFS/Target/bsp_driver_sd.c) now prepares the complete SDMMC1 handle, initializes at one bit and negotiates four bits. Absent/failed media is optional, not a startup crash. [The storage owner](../App/Src/atlas_storage.c) owns mount/unmount/read/append and the [disk port](../FATFS/Target/sd_diskio.c) permits initialization only after explicit mount authorization.

## R04: SD read completion can be erased

The old callback/DMA read path is replaced by synchronous polling. There is no “launch then erase completion” flag. A private aligned sector buffer accommodates unaligned callers; no caller is retained asynchronously. The obsolete weak-callback host wrapper was retired, with its original preserved in the pre-correction backup.

## R05: SD timeouts do not establish buffer release

SD no longer uses IDMA. Errors reset/quiesce the controller, invalidate the mounted generation and reject old-session operations. Both mechanical detect edges count, including rapid remove/reinsert. The owner checks media before queued file work; disk status invalidates FatFs cached metadata and ST's disk-init cache. A file operation cannot silently remount a replacement card.

Transfer waits are 250 ms/sector and readiness waits 1000 ms with yielding; HAL internal commands/preemption can extend total wall time. A partial/failed append has an uncertain outcome and is never automatically retried.

[The real FatFs/BSP/disk RAM-volume test](../Tests/review/test_sd_integration.c) covers initialization, immediate polling completion, unaligned reads/writes, ranges, timeout/reset, ready waits, absence, replacement/removal, append preservation, cached-file rejection after replacement, and successful **explicit** remount. [The storage-owner test](../Tests/services/test_storage_owner.c) adds queue backpressure, removal-before-write, short append, failed sync and partial RTC updates.

## R06: Default memory is not SDMMC1 or DMA1 accessible

DTCM remains appropriate for CPU-owned data/stacks. SD uses CPU polling and USB PIO. ADC1 and TIM6's GPIO sequencer use two 32-byte private blocks in AXI SRAM `.atlas_dma`, explicitly placed by [GNU](../STM32H743XX_FLASH.ld) and [IAR](../EWARM/stm32h743xx_flash.icf). Complete 64-bit ECC words are initialized before subword DMA access.

[AtlasIo_Start](../App/Src/atlas_io.c) checks exact handles, buffer bounds, reviewed clocks, factory calibration and D-cache-off startup. Completion/abort must establish quiescence before reuse; failed buffers are quarantined. DMA1-to-GPIO D3 connectivity and memory/ECC assumptions were checked against the bundled HAL and [ST AN4891](https://www.st.com/resource/en/application_note/dm00306681.pdf)/[AN5342](https://www.st.com/resource/en/application_note/dm00623136-error-correction-code-ecc-management-for-internal-memories-protection-on-stm32h7-series-stmicroelectronics.pdf). Actual DMA routing, cache policy, ECC behavior and pulse width remain inert bench requirements.

Pass 2 additionally found that IAR left CSTACK placement implicit despite the fixed MPU guard. It now explicitly places the 16 KiB main stack at 0x2001C000 and excludes guard/stack from ordinary data. GNU asserts the same stack base and rejects data reaching 0x2001BF00. A link-only negative control exceeds the guard boundary while still fitting ordinary DTCM capacity; it correctly fails the new guard assertion.

The generated C-library allocator also ignored the zero-heap reservation and could grow toward that guard. [The replacement boundary](../Core/Src/sysmem.c) rejects all heap changes with ENOMEM; [its host test](../Tests/services/test_sysmem.c) covers zero, positive, negative and extreme requests. Radio/BLE AT formatting now uses bounded integer/string construction, with shortest/longest name and zero/maximum integer wire checks. No allocator/printf path remains linked in the current GNU images.

## R07: USB is not a complete or started CDC service

[AtlasUsb](../App/Src/atlas_usb.c) provides VBUS debounce/recheck, prompt disconnect and task-owned teardown, a 1024-byte RX ring and four copied TX packets. Sessions invalidate stale data; a 2000 ms TX timeout tears down without replay. The CDC adapter validates line coding/control requests, retains TX memory until final data/ZLP completion and safely rejects pre-enumeration access.

Review also corrected malformed direction/interface/size handling, short control OUT payloads, failure-to-stall/incorrect status ACKs, stack-lifetime EP0 data, two-byte endpoint status, rejected TX launch and missing EP2 TX FIFO. The explicit local middleware changes are recorded in [Provenance](reference/PROVENANCE.md#local-integration-changes-2026-09-02).

[The CDC test](../Tests/services/test_usb_cdc.c) compiles the real adapter plus ST core/class. [The owner test](../Tests/services/test_usb_owner.c) scripts initialization-time VBUS loss, unplug, overflow, result lifetime, timeout and re-enumeration without replay. Neither emulates electrical USB or substitutes for actual host/OS enumeration. There is a byte service, not a supplied console or trusted command protocol.

## R08: GNSS parser has no truncated-frame age limit

[The parser](../App/Src/atlas_gnss.c) rejects payload lengths above its 512-byte bound immediately, enforces 250 ms interbyte and 2000 ms whole-frame limits, and flushes/reset-resynchronizes after transport overflow/restart. Age is observed at service time; UART bytes do not carry individual wire timestamps.

Tests include clean/fragmented NAV-PVT, embedded sync bytes, invalid checksum, truncated in-range/oversized frames, elapsed-frame limit, clock wrap and transport loss. A valid packet after the formerly blocking header is parsed; discarded-length state is zero.

## R09: Hardware labels and power-protection claims

The owner confirmed RFD900x at the “LoRa” J9 connector. Documentation explicitly identifies J5's feed from VIN_RAW to PYRO_VBAT_ARMED as **bypassing TPS16850 U11**.

The schematic routes logical `LED_G`/PB7 to D5 blue and `LED_B`/PD14 to D5 green. Firmware is not silently remapped without exact fitted-part/as-built evidence. That physical discrepancy remains unresolved; identify states by enum/GPIO/debugger until measured. No schematic or manufacturing output was changed.

## R10: Deadline checks do not cover release lateness

Control now outranks blocking sensor/link work. Scheduled release—not actual wake time—starts the 10 ms response budget, including snapshot wait and hook. Late work inhibits outputs, latches a fault and skips catch-up bursts. Non-maintenance sensor cycles at least 20 ms also fault. Slow conversions/commands/expansion do not stack in one cycle, and stream UART waits are capped at 10 ms.

A lock-free output gate expires at the earliest required sensor age; the output owner additionally requires fresh calibrated ADC and valid rails. Maintenance rejects armed/active outputs, serializes the hold against enable, and keeps control/actuation inhibited during the bounded operation and recovery.

Pure deadline/supervisor tests pass. No full-kernel scheduling simulation or physical WCET/jitter/stack-load evidence exists; those remain mandatory.

## R11: Configured peripherals are not complete services

Added services and their [central contracts](PERIPHERALS.md):

- Static SD/UTC owner with explicit removable-media lifecycle and append/read results.
- VBUS-aware USB byte transport and validated CDC adapter.
- Calibrated asynchronous ADC, raw inputs, allowed-mask GPIO and KST PWM service.
- Hardware-independent pyro policy plus separate timer/DMA adapter, continuous qualified continuity, software/physical-supply gates, four-attempt per-channel/boot cap and conservative OFF guard.
- Sole-owner raw UART4/I2C2/SPI3 expansion work.
- Queued radio identity/settings/parameter/host-baud and BLE SPS commissioning; no automatic persistence.

Safety review found and corrected OFF-command generation fencing and ECC duplicate-event accounting. The first ECC monitor/offset/type is retained; HAL's accumulated event code is consumed only after evidence capture.

The stock image does not claim qualified servo neutral/travel, pyro voltage/continuity thresholds, successful deployment, RF peer service, navigation suitability, application framing/authentication or flight algorithms. These are explicit remaining configuration/qualification work, not hidden automatic defaults.

## Executable evidence

The review uses Arm GNU 14.3.1 (Arm build 14.174), CMake 3.26.4, MinGW Makefiles/GNU Make, host GCC 13.1.0 and PowerShell 7. No IAR compiler or physical board is available.

| Check | Delivered-checkout result | Boundary |
|---|---|---|
| Fresh GNU Debug and Release builds | Pass; zero compiler warnings in both | Complete linked firmware; figures below |
| Strict target compile/static analysis | Pass on 37 translation units | 23 App files, 10 project adapters and 4 USB middleware files; `-O0 -Wall -Wextra -Werror -fanalyzer` |
| Host protocol suite | Pass | 15 test groups, including added BLE name and radio integer-format boundaries |
| Focused review probes | Pass, exit 0 | Real timing/GNSS/SD/FatFs and pure analog/pyro logic with injected boundaries |
| Service suite | Pass, exit 0 | Real output/USB/storage/expansion owners, real USB core/class and rejecting C-library heap boundary |
| Repository checker | Pass; 309 local links and 61 linked heading anchors | Doxygen, build membership, artifact/static-memory/stack contracts |
| Documentation examples and API references | Four C blocks compile against target headers; 136 named API references resolve | Three peripheral examples plus the application hook; not physical execution |
| Preservation | Pass | 276 untouched files byte-identical; 98 changed/new text files match reviewed staging; one backed-up wrapper retired |
| Physical/IAR acceptance | Not performed | No electrical, RF, flight, complete-kernel simulation or IAR-binary claim |

The analyzer initially reported the deliberate terminal loops in `Error_Handler` and the fatal exception handlers. Their preceding emergency-inhibit paths were inspected. Only `-Wanalyzer-infinite-loop` is disabled for `main.c` and `stm32h7xx_it.c` in this audit; all other diagnostics remain errors. Formatting scratch buffers are initialized, and the final audit has no unexplained diagnostics. The audit does not prove all paths correct.

Run the commands in [Quick start](QUICK_START.md#2-run-the-complementary-checks), including the added bring-up suite. The old expected-failure baseline is archived; every current behavioral assertion must pass. Do not weaken a fixture just to obtain green output.

### Delivered GNU build evidence

These fresh builds use the actual Desktop repository, not merely the staging copy. Build products and logs are retained outside the repository.

| Configuration | FLASH bytes | DTCM bytes | AXI DMA bytes |
|---|---:|---:|---:|
| Debug | 201616 | 89792 | 64 |
| Release | 104008 | 89768 | 64 |

DTCM figures include the linker's main-stack reservation. The main stack starts at 0x2001C000, ends at 0x20020000 and has its guard at 0x2001BF00. The two private DMA blocks are each 32-byte-aligned in the 64-byte AXI NOLOAD section starting at 0x24000000; their internal order differs between configurations and is not assumed by the code. Release disassembly confirms full-width `STRD` ECC-seeding stores.

Both images contain exactly one strong SVC, PendSV, SysTick, ADC, DMA1 stream 0/1 and OTG_FS handler. No libc allocator, heap-growth, printf or FreeRTOS allocator entry point is linked. A separate positive/negative link control proves the GNU guard assertion rejects a data allocation that still fits ordinary DTCM capacity but enters the guard.

The largest observed individual Debug stack frames are 1104 bytes for the storage task, 888 for the supervisor and 640 for the application task. These are **not** complete call-chain, FPU, interrupt, or runtime stack-margin proofs.

SHA-256 of these exact local build artifacts:

```text
Debug ELF   2C93712CD73EA007A37371ED92C130D8D4647A7BDF6C719D127227148513E0E0
Debug map   BAE5F3EE6C747E6468ED4E6D23E8A3AE81E5A355C43CDA6907012C57027258CC
Release ELF 341B9D2926FBE2EDC95A58CC802D85D4AE2A29C90ED33774486525DDE3A66EAB
Release map 3BE8DD9080875729621BED196FC54B744AA1E9715CADF2D08CC53B060C7E8721
```

Compiler diagnostics/debug metadata include source locations; relocating the source tree can change hashes and diagnostic-string footprint. Reproduce with the recorded toolchain and compare executable behavior/configuration, not an unrelated machine's hash alone.

### Documentation and delivery controls

The repository checker passed positive controls for compact Doxygen blocks, pointer-return functions, duplicate heading suffixes and ignored fenced examples. Seven negative controls were then correctly rejected: a missing visible file, missing heading anchor, missing parameter tag, missing pointer-return tag, new kernel FromISR call, libc formatted I/O, and an inconsistent IAR stack base. Restoring those controls returned the checker to a pass. Control fixtures were isolated outside the delivered repository.

The current navigation has two entry documents (Quick start and Systems), with one central peripheral guide and one engineering-standards guide. Device details remain optional references; the archive is explicitly historical. A fresh-reader pass checked startup versus runtime behavior, units/channel numbering, queue acceptance versus execution, persistence, failure handling, and qualification prerequisites.

Before publication, all 343 existing repository files matched the preserved snapshot: no concurrent target edits were overwritten. The correction changed 66 files, added 32 and retired only `Tests/review/sd_bsp_host.c`, leaving 374 delivered files excluding Git internals. All 276 files outside that change set remain byte-identical. All 19 manufacturing-manifest hashes pass. Changed/new text was compared with reviewed staging after newline normalization; raw hardware exports were not normalized or changed. `git diff --check` passes.

The pre-correction snapshot, including the retired wrapper, remains in the external backup `atlas_before_fixes_20260901.zip`, SHA-256:

```text
88513F2E979303E0A7E8C59A9C2581F23D2304B97360AC5EA41EAD2AC67FAB3A
```

Existing checkout changes were retained. No files were staged or committed, no branch was changed, and the separate original Atlas folder was not modified.

## Remaining release gates

1. Exact board/load electrical qualification, measured servo neutral/travel, arm-voltage/continuity thresholds, supply tolerances and inert timer/DMA cutoff evidence.
2. Actual sensor/GNSS/SD/USB/BLE/RFD900x communications and failure/recovery tests, including navigation quality and peer operation.
3. Measured scheduling, memory, ISR and stack margins under worst-case combined traffic and fault conditions; actual IAR build if used.
4. As-built LED mapping decision, authorized USB product identity, application logging/framing/security and approved flight-state logic.
5. Qualified independent safety review and a responsible-owner release decision. Three author passes and green host tests do not satisfy that release approval.

No physical firing, actuator activation, RF transmission, board flashing, module nonvolatile configuration or original Atlas-folder edits were performed.
