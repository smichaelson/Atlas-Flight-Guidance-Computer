# Review 3 — Documentation, Onboarding, and Repository Hygiene

| Field | Value |
|---|---|
| Date | 2026-09-01 |
| Scope | New-collaborator path, claim/evidence consistency, module guides, code documentation, links, forbidden artifacts, and generated-output hygiene |
| Method | Read-through from a new collaborator's perspective plus repeatable repository checks |
| Outcome | Pass for repository/document consistency; external URLs and physical procedures require ongoing maintenance |

## Questions applied

1. Can a new collaborator determine what Atlas is, what is implemented, what is unverified, where each API lives, and how to build/test without relying on oral history?
2. Does every fitted sensor, major communications module, and user-feedback output have a guide containing wiring, startup behavior, API contract, units/format, health interpretation, known limits, primary references, and bench acceptance?
3. Do documentation claims distinguish protocol-tested, target-built, bench-verified, and flight-qualified status?
4. Are project-owned firmware files headed by purpose/major-function indexes, and are functions documented with Doxygen data tags and important inline rationale?
5. Are raw KiCad sources, obsolete Atlas_Origins material, build products, machine-local paths, and broken local links absent from the deliverable?

## Findings resolved in this baseline

- **R3-01 — Stale validation descriptions:** The validation ledger and sensor/output guides understated the expanded direct-register, command-flow, LED, and buzzer host tests. The matrix now names the exact tested behavior without claiming physical verification.
- **R3-02 — Stale memory figures:** Build documentation was synchronized to the final clean images: Debug 121,412 bytes flash / 31,440 bytes DTCM and Release 59,908 bytes flash / 31,440 bytes DTCM.
- **R3-03 — Header consistency:** The shared status files now use the same “Major functions” convention as other project-owned units, and the generated `main.c` user header lists its Atlas integration responsibilities.
- **R3-04 — Failure semantics documentation:** BLE response overflow and RFD900x echo-only identity/settings rejection are now stated in API comments, module health tables, tests, and status evidence.

## Onboarding path reviewed

The intended sequence is [README](../../README.md) → [Project Status](../PROJECT_STATUS.md) → [Engineering Standards](../../STANDARDS.md) → [Module Firmware Guides](../modules/README.md) → [Firmware Architecture](../FIRMWARE_ARCHITECTURE.md) → [Building](../BUILDING.md) → [Bring-up](../BRINGUP.md) → [Validation Status](../VALIDATION.md).

The path was checked for consistent vocabulary, safety boundaries, source authority, build prerequisites, debugger entry points, module ownership, persistent side effects, and handoff to physical acceptance.

## Repeatable checks

Run from the repository root:

```text
powershell -ExecutionPolicy Bypass -File Tests/repository/check_repository.ps1
```

The check resolves every local Markdown target, requires the core onboarding/review documents, verifies Doxygen file/major-function headers and `@brief`/`@param`/`@return` tags for `App/` definitions, and rejects raw KiCad source extensions, Atlas_Origins paths, target build artifacts outside excluded build directories, and related repository debris. It passed for this reviewed snapshot.

Additional pass-specific scans found no project-owned use of machine-local `C:\Users\...` links and no stale prior memory figures. Occurrences of “LoRa” remain only where the documentation explicitly corrects that misconception.

## Deliverable policy confirmed

- No raw KiCad schematic/PCB/project file is included.
- No file or directory named for obsolete Atlas_Origins material is included.
- The revision-matched schematic PDF, BOM, and candidate manufacturing outputs remain clearly labeled; they are not presented as verified as-built provenance.
- Generated build directories, ELF/HEX/BIN/map/object/stack-usage files are not part of the copy set.
- The source tree, module guides, tests, standards, review evidence, and supported build membership are included together.

## Residual hold points

- External manufacturer links were selected from primary sources, but upstream URLs can move and should be checked during release preparation.
- Documentation cannot validate the as-built board, radio region, module firmware, calibration, timing, or fault recovery; each module guide deliberately ends in a physical acceptance procedure.
- These records are self-review evidence. They do not replace independent human review, requirements traceability, or flight qualification.
