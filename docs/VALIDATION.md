# Firmware Validation Status

## Purpose

This ledger separates implementation evidence from physical proof. It prevents a successful compile, code review, or LED indication from being misreported as verified hardware communication.

## Controlled terms

| Term | Required evidence |
|---|---|
| Implemented | Source, API, build membership, and documentation exist. |
| Protocol-tested | Deterministic host tests exercise the stated framing, checksum, packing, or math behavior. |
| Target-built | The complete STM32 image compiles and links for the target without warnings. |
| Bench-verified | A named board/module revision passed the documented physical procedure with retained evidence. |
| Flight-qualified | Approved requirements, calibration/environmental limits, fault behavior, traceability, and release authority all exist. |

Only the exact evidence stated may be inferred. For example, a parser can be protocol-tested while the UART electrical path remains unverified.

## Current matrix

| System | Implemented | Protocol-tested | Target-built | Bench-verified | Acceptance guide |
|---|:---:|:---:|:---:|:---:|---|
| ADXL375 | Yes | Register/config/sample | Yes | No | [ADXL375](modules/ADXL375.md) |
| LSM6DSV16B | Yes | Register/config/sample | Yes | No | [LSM6DSV16B](modules/LSM6DSV16B.md) |
| MMC5983MA | Yes | Register/config/sample | Yes | No | [MMC5983MA](modules/MMC5983MA.md) |
| MS5611 | Yes | Command/PROM/CRC/compensation | Yes | No | [MS5611](modules/MS5611.md) |
| BNO085 + CEVA SH-2 | Yes | No | Yes | No | [BNO085](modules/BNO085.md) |
| NEO-M9N GNSS | Yes | UBX/NAV/config/PPS | Yes | No | [GNSS](modules/GNSS.md) |
| NINA-B112 BLE | Yes | AT/profile/mode/overflow | Yes | No | [BLE](modules/BLE.md) |
| External RFD900x | Yes | AT/identity/settings/S-register | Yes | No | [RFD900x](modules/RFD900X.md) |
| RGB LED | Yes | GPIO ownership/state | Yes | No | [LED](modules/LED.md) |
| Differential buzzer | Yes | Timer/mode/deadline | Yes | No | [Buzzer](modules/BUZZER.md) |
| UART transport | Yes | Overflow accounting | Yes | No | [Architecture](FIRMWARE_ARCHITECTURE.md) |
| `AtlasBoard` integration | Yes | No | Yes | No | [Bring-up](BRINGUP.md) |
| FreeRTOS task/ownership layer | Yes | Timing/watchdog policy | Yes | No | [RTOS architecture](RTOS_ARCHITECTURE.md) |

Nothing in this matrix is flight-qualified.

## Automated evidence — 2026-09-01

- Host suite: passed under MinGW-w64 GCC 13.1.0 as C11 with `-Wall -Wextra -Werror`.
- Arm Debug: clean compile/link under Arm GNU 14.3.1; 146,344 bytes flash and 51,976 bytes DTCM.
- Arm Release: clean compile/link under Arm GNU 14.3.1; 74,064 bytes flash and 51,976 bytes DTCM.
- IAR project: XML and source/include membership inspected; IAR compiler unavailable, so build status is pending.
- CEVA SH-2: source pinned to commit `b514b1e2586ddc195e553dac89fc94c637b25298`; local upstream files unmodified.
- FreeRTOS: V10.6.2 subset copied unmodified from local STM32CubeH7 1.13.0; GNU/IAR Cortex-M7 r0p1 ports selected; MIT license retained.
- Link/symbol review: exactly one SVC, PendSV, SysTick wrapper, and kernel tick handler is present; C/FreeRTOS heap allocation symbols are absent; all RTOS objects and stacks are static.
- Strict source review: all 19 project-owned firmware/integration translation units pass `-Wall -Wextra -Werror` and GCC `-fanalyzer`; unchanged generated/vendor units are not included in that stricter claim.
- Host RTOS policy: wrap-safe periods/sample ages, fresh/stale supervisor inputs, healthy supervision, active/expired declared long I/O, startup/service/sampling failures, both required-task stalls, and all four task-stack margin paths pass with strict host warnings. The per-sensor required-bit/age mapping is source-reviewed rather than host-executed.

Build output is reproducible using [Building Atlas firmware](BUILDING.md). Generated local build products are not retained in Git.

## Review evidence

The communication baseline retains three reviews, and this RTOS integration adds three more:

1. [Hardware and protocol conformance](reviews/REVIEW_1_HARDWARE_PROTOCOL.md)
2. [Implementation, failure paths, and builds](reviews/REVIEW_2_IMPLEMENTATION_BUILD.md)
3. [Documentation, onboarding, and repository hygiene](reviews/REVIEW_3_DOCUMENTATION_ONBOARDING.md)
4. [RTOS architecture, timing, ownership, interrupts, and watchdog](reviews/REVIEW_4_RTOS_ARCHITECTURE.md)
5. [RTOS implementation, concurrency, fault paths, tests, builds, and memory](reviews/REVIEW_5_RTOS_IMPLEMENTATION_BUILD.md)
6. [RTOS documentation, onboarding, source membership, and repository hygiene](reviews/REVIEW_6_RTOS_DOCUMENTATION_ONBOARDING.md)

These reviews improve confidence and expose assumptions. They do not substitute for physical measurements.

## How to promote a module to bench-verified

1. Use the exact board revision, part number, module firmware, and source revision named in the evidence record.
2. Follow [Module bring-up and acceptance](BRINGUP.md) and the module-specific acceptance section without omitting fault cases.
3. Capture decoded bus traffic and the relevant electrical timing/reset/interrupt waveform.
4. Record expected and observed identities, configuration readbacks, data rates, sample plausibility, counters, and timeout behavior.
5. Attach trace files or stable links and have another qualified person review them.
6. Update this matrix and the module guide in the same pull request.

If any condition differs later—board rework, module firmware, clock configuration, driver change, or toolchain change—state which evidence remains applicable and rerun the affected tests.
