# Review 5 — RTOS Implementation, Concurrency, Build, and Memory

## Review identity

- **Review performed:** 2026-09-01
- **Scope:** project-owned RTOS implementation, task/ISR concurrency, queues and streams, fault paths, static memory, source membership, host tests, Arm builds, and linked-image symbols
- **Method:** fresh line-by-line source pass, lock/owner and preemption analysis, bounded-loop search, strict host tests, clean Debug/Release Arm GNU builds, GCC static analysis, linker-symbol/disassembly inspection, compiler stack-usage review, IAR XML/path inspection, and vendor-file hashing
- **Result:** pass at source/build level after the corrections below; IAR compilation and physical execution remain pending

This is a rigorous author self-review, not an independent qualified reviewer or hardware acceptance test.

## Questions applied

1. Can any task, callback, or hook concurrently enter a driver or split a bus/module transaction?
2. Can a queue, mutex, stream, parser, retry, or interrupt-driven snapshot block or retry without a fixed bound?
3. Can any ordering window make the supervisor accept stale data, report a false stall, or resume watchdog refresh after a fault?
4. Are all scheduler resources fixed-size, correctly aligned/provisioned, and represented in both supported build systems?
5. Does the linked image contain exactly one exception implementation, one watchdog refresh call site, and no heap allocator?
6. Are stack allocations defensible from compiler evidence and guarded by live high-water supervision?
7. Are test/build claims limited to what the available toolchains and hardware actually prove?

## Findings and corrections

| ID | Severity | Finding | Correction and disposition |
|---|---|---|---|
| R5-01 | High | A queued LED, buzzer, radio, or BLE request could otherwise reach hardware after startup/service/sampling or supervisor health had already failed. | The I/O owner checks running state, the monotonic RTOS fault, and all three aggregate statuses after dequeue. A rejected request completes with `ATLAS_ERROR_STATE` and does not call a driver. Closed; an operation already in progress remains bounded by its reviewed call/deadline contract. |
| R5-02 | High | At long-operation completion, clearing `io_busy` before publishing sensor recovery left a preemption window in which the supervisor could see neither a maintenance exception nor a fresh I/O heartbeat and diagnose a false stall/stale condition. | Recovery state is now published first. Completion advances the I/O heartbeat in the same health-mutex section that clears the busy gate, so every supervisor observation sees a valid side of the handoff. Closed pending bench transition stress. |
| R5-03 | Medium | The implementation and README claimed every task stack margin was supervised, but the static FreeRTOS idle task was covered only by overflow checking. | Enabled idle-task-handle access, added its high-water field to policy/health, and require the same 64-word minimum as the three Atlas tasks. Added the low-idle-stack host-policy case. Closed pending live high-water measurement. |
| R5-04 | Medium | The GNSS PPS coherence reader used an unbounded retry loop. Normal 1 Hz input made starvation implausible, but a noisy capture input violated the repository’s bounded-loop rule. | Limited the lock-free copy to three attempts and fail closed for that publication; the next I/O cycle retries naturally. PPS is not a watchdog-required sensor. Closed. |
| R5-05 | Medium | A redundant request to enter a module mode already active could unnecessarily open a long-operation/recovery interval and defer freshness. | Long-operation deadlines and recovery are now opened only for an actual BLE/RFD state transition; already-satisfied requests use the driver’s immediate idempotent path. Closed. |
| R5-06 | Medium | A long transition could return after its deadline between supervisor periods, and fatal diagnostic writes could be preempted before watchdog feeding stopped. | The I/O task checks the saved deadline again on return. Assertion and stack-overflow hooks disable interrupts before recording available context. Closed; also traced in [Review 4](REVIEW_4_RTOS_ARCHITECTURE.md). |
| R5-07 | Low | Build parity and static-allocation rules depended on manual inspection alone. | The repository checker now rejects heap implementations, direct App-driver `HAL_Delay()`, and unreviewed `...FromISR()` calls; requires RTOS documents/files; verifies all App sources and kernel/port inputs in CMake/IAR; rejects cross-selected ports; parses the IAR XML; and resolves every referenced IAR file. Closed. |

## Build and test evidence

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

## Memory and stack review

All tasks, stacks, queue storage, mutex control blocks, and stream-buffer storage are application-static. The stream arrays provide 1,025 physical bytes each to the FreeRTOS ring implementation, yielding the documented 1,024-byte usable capacity.

Debug compiler direct-frame output reports:

| Task entry | Direct frame | Static stack | Interpretation |
|---|---:|---:|---|
| `atlas_rtos_io_task` | 40 bytes | 8,192 bytes | Deep driver callees dominate; direct size alone is not a call-depth bound. |
| `atlas_rtos_application_task` | 632 bytes | 4,096 bytes | The future control-hook call tree must fit inside the same task stack. |
| `atlas_rtos_supervisor_task` | 696 bytes | 2,048 bytes | Includes four high-water queries and coherent policy inputs. |
| FreeRTOS idle | kernel-dependent | 1,024 bytes | Performs no Atlas hook; live margin is now supervised. |

Notable project callee frames include 568 bytes for `AtlasGnss_SendUbx`, 392 bytes for `AtlasMs5611_Compensate`, and 328 bytes for `AtlasBle_Command`. These figures are compiler-local frames, not whole-call-chain proofs. The 64-word supervisor threshold and the initial allocations remain hypotheses until live high-water marks are retained under worst-case traffic and fault paths.

## Residual limits

- No IAR compiler was available; only project structure, options, XML, and referenced files were checked.
- Address/undefined-behavior sanitizers were unavailable in the installed MinGW toolchain, so no sanitizer result is claimed.
- No assembled Atlas board was available. Bus electrical behavior, module firmware compatibility, throughput/backlog behavior, timing/jitter, live stack margins, and watchdog reset timing remain governed by [Module bring-up and acceptance](../BRINGUP.md).
- Source/build review cannot prove sensor accuracy, RF delivery, BLE pairing, GNSS fix quality, or safe flight behavior.

## Conclusion

The reviewed RTOS implementation is internally consistent, statically provisioned, warning-clean in the stated scopes, and fail-closed at its software supervision boundaries after the corrections above. It is a strong integration baseline, not hardware or flight qualification. Review 6 separately audits documentation, onboarding, links, and repository hygiene.
