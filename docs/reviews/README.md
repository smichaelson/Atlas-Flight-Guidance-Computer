# Review Records

This directory preserves six distinct review passes performed for the 2026-09-01 Atlas communication-firmware and RTOS baselines. Each pass uses a different question set and records both corrections and unresolved evidence gaps.

| Pass | Scope | Record |
|---|---|---|
| 1 | Schematic mapping and manufacturer-protocol conformance | [Hardware and protocol](REVIEW_1_HARDWARE_PROTOCOL.md) |
| 2 | Implementation safety, failure paths, tests, builds, and build membership | [Implementation and build](REVIEW_2_IMPLEMENTATION_BUILD.md) |
| 3 | Documentation accuracy, onboarding, links, and repository hygiene | [Documentation and onboarding](REVIEW_3_DOCUMENTATION_ONBOARDING.md) |
| 4 | RTOS architecture, timing, ownership, interrupts, and watchdog policy | [RTOS architecture and supervision](REVIEW_4_RTOS_ARCHITECTURE.md) |
| 5 | RTOS implementation, concurrency, fault paths, builds, and memory | [RTOS implementation and build](REVIEW_5_RTOS_IMPLEMENTATION_BUILD.md) |
| 6 | RTOS documentation, onboarding, source membership, and repository hygiene | [RTOS documentation and onboarding](REVIEW_6_RTOS_DOCUMENTATION_ONBOARDING.md) |

These are sequential technical self-review passes completed while preparing the baselines. They are not six independent reviewers and do not satisfy the qualified human approvals required by [Atlas Engineering and Collaboration Standards](../../STANDARDS.md) for safety-relevant release work. Physical acceptance and independent review remain mandatory.

Future reviewers should create a new dated record rather than silently rewriting historical results. If a correction changes the applicability of one of these records, state that explicitly in [Firmware Validation Status](../VALIDATION.md).
