# Review 6 — RTOS Documentation, Onboarding, and Repository Hygiene

## Review identity

- **Review performed:** 2026-09-01
- **Scope:** collaborator entry path, RTOS/API contract, module guides, build/validation claims, function documentation, cross-links, source membership, third-party provenance, and excluded-artifact policy
- **Method:** fresh reader walkthrough from README to first control-hook integration; source-to-document comparison; terminology/number search; module-guide coverage matrix; local-link and Doxygen-tag automation; CMake/IAR parity check; and repository artifact scan
- **Result:** pass after the corrections recorded below

This is the third RTOS-specific author self-review and the sixth sequential repository review. It is not an independent reviewer or physical acceptance.

## Reader questions applied

1. Can a new collaborator find the current status, safety boundary, build/test path, RTOS ownership rules, and first application integration point without relying on oral history?
2. Does each fitted sensor, communications module, and feedback output explain both its direct driver and the supported post-scheduler interface?
3. Are time domains, validity, freshness, maintenance, asynchronous completion, overflow, persistence, and peer/link limitations stated without ambiguity?
4. Do build sizes, toolchain status, test scope, and validation labels match the final evidence exactly?
5. Do all project functions carry the requested file header and Doxygen `@brief`, `@param`, and `@return` tags?
6. Can automation prevent raw KiCad sources, obsolete Atlas_Origins material, generated binaries, RTOS heap code, or build-membership drift from entering unnoticed?

## Findings and corrections

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

## Documentation coverage

| Need | Authoritative location | Review result |
|---|---|---|
| First orientation and safety boundary | [Repository README](../../README.md) | Current RTOS capability and non-claims are visible before build instructions. |
| Coding, concurrency, review, and release rules | [Engineering standards](../../STANDARDS.md) | Static ownership, deadlines, freshness, interrupts, watchdog, evidence, and review gates are explicit. |
| Task graph and application API | [RTOS architecture](../RTOS_ARCHITECTURE.md) | Tasks, priorities, rates, snapshots, commands, streams, maintenance, interrupts, memory, faults, and bench acceptance are complete. |
| Full firmware layering/failure model | [Firmware architecture](../FIRMWARE_ARCHITECTURE.md) | Generated/project/vendor boundaries and failure consequences agree with source. |
| Reproducible tool/test path | [Building](../BUILDING.md) | Tool versions, clean sizes, commands, output checks, IAR limitation, and troubleshooting are current. |
| Physical acceptance | [Bring-up](../BRINGUP.md) | Safe setup, all modules, RTOS load/fault cases, and retained evidence template are present. |
| Claim vocabulary and matrix | [Validation status](../VALIDATION.md) | Implemented/protocol-tested/target-built/bench-verified/flight-qualified remain distinct. |
| Current boundary and backlog | [Project status](../PROJECT_STATUS.md) | Implemented, pending physical work, and deliberately absent control/safety functions are separated. |
| Dependencies and update discipline | [Third-party provenance](../THIRD_PARTY.md) | CEVA and FreeRTOS source/version/license/update procedures are recorded. |

All ten device/output guides—ADXL375, LSM6DSV16B, MMC5983MA, MS5611, BNO085, GNSS, BLE, RFD900x, LED, and buzzer—contain a major-functions index, conservative validation state, explicit RTOS access/ownership section, board/protocol contract, health/fault interpretation, known limits, primary references, and repeatable bench acceptance. Direct examples are labeled pre-scheduler/isolated where post-start driver access would violate ownership.

## Automated hygiene evidence

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

## Residual limits

- The checker validates local Markdown targets but does not fetch or archive external manufacturer/FreeRTOS URLs.
- Documentation cannot validate electrical wiring, exact fitted part/firmware revision, real-time margins, or module communication. Those remain physical acceptance work.
- The records are sequential self-reviews, not the independent qualified approvals required by [Engineering standards](../../STANDARDS.md) for safety-relevant release.
- The project still has no selected project-level license; the README warns collaborators not to infer reuse rights.

## Conclusion

A new collaborator now has a coherent, source-matched path from repository purpose and safety limits through RTOS application integration, module operation, build/test evidence, and physical acceptance. Automated checks cover the most likely documentation, source-membership, static-allocation, and artifact-policy regressions. The repository is ready for collaborative implementation work at the documented development maturity; it is not yet bench-verified or flight-qualified.
