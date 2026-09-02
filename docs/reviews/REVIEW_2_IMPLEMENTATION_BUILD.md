# Review 2 — Implementation, Failure Paths, and Builds

> Applicability note (2026-09-01 RTOS integration): references below to the former foreground main loop are historical. Review 4 supersedes that execution model with a highest-priority RTOS supervisor as the sole watchdog owner.

| Field | Value |
|---|---|
| Date | 2026-09-01 |
| Scope | Project-owned firmware, CEVA integration, target build inputs, host tests, bounded-failure behavior, and memory evidence |
| Method | Fresh source pass, strict host compile/tests, clean Debug/Release Arm GNU builds, source-membership/XML checks, API scan, and compiler stack-frame inspection |
| Outcome | Pass for the tested toolchain; IAR compilation and hardware execution remain pending |

## Questions applied

1. Are externally supplied lengths, indices, parser states, queues, waits, and response buffers bounded and fail-visible?
2. Do interrupts remain short, with protocol parsing and blocking bus work deferred to foreground context?
3. Can a partial response, command echo, asynchronous reset, ring overflow, or secondary-channel failure be reported as success?
4. Do runtime failures reach the board-level fault latch and prevent unconditional watchdog refresh?
5. Are all project-owned and pinned CEVA units present in both supported build descriptions?

## Findings resolved in this baseline

- **R2-01 — BLE response truncation:** A caller response buffer overflow was counted but could previously be followed by terminal `OK`. `AtlasBle_Command()` now returns `ATLAS_ERROR_OVERFLOW` immediately for either line or destination truncation; a regression test proves `command_ok` remains unchanged.
- **R2-02 — Radio echo-only evidence:** `ATI`/`ATI5` terminal `OK` with only local command echo did not prove an identity/settings response. The typed helpers now require substantive non-echo content and increment `malformed_responses` on failure.
- **R2-03 — Coherent PPS reads:** Foreground PPS consumers use a short critical-section snapshot so an interrupt cannot produce a mixed capture/period/count structure.
- **R2-04 — Partial buzzer start:** If TIM15 channel 2 fails, channel 1 is stopped before the driver returns an error; stop remains idempotent.
- **R2-05 — Runtime service visibility:** BNO085 service/counter changes, UART service failures, and buzzer service failures latch `AtlasBoard.runtime_fault`; the main loop stops refreshing the watchdog after a startup or runtime fault.

## Automated evidence

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

## Design observations

- UART receive paths are allocation-free single-producer/single-consumer rings. ISRs capture bytes/errors and restart reception; foreground code parses protocols.
- SPI chip-select scope covers complete transactions. No dynamic allocation is used by the Atlas application layer.
- Device waits, conversion polls, command transactions, and startup probes use explicit time bounds. The intentional infinite loops are the main foreground scheduler and generated fatal `Error_Handler()` fail-stop.
- Ordinary startup performs no RFD900x AT command, no BLE save, and no GNSS nonvolatile write.
- Configuration APIs separate volatile setup from explicit persistence and document the NVM side effect.

## Residual hold points

- IAR Embedded Workbench was unavailable, so XML/source membership was checked but no IAR object or map was produced.
- There is no static whole-call-graph stack proof, measured worst-case execution time, scheduler deadline analysis, or hardware-in-the-loop run.
- Host HAL emulators prove software handling of defined transactions; they do not prove electrical timing, silicon behavior, clock accuracy, or recovery from every physical fault.
- Independent qualified review remains required by [Engineering and Collaboration Standards](../../STANDARDS.md), especially before output, watchdog, interrupt, or control behavior is released.
