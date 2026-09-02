# Review 4 — RTOS Architecture, Timing, and Safety Supervision

## Review identity

- **Review performed:** 2026-09-01
- **Scope:** FreeRTOS selection/configuration, task graph, ownership boundaries, interrupt contract, timing/deadline logic, sensor freshness, and independent-watchdog policy
- **Method:** line-by-line design review, call-site searches, target symbol inspection, deterministic host-policy tests, and comparison against the locally installed STM32CubeH7 FreeRTOS source
- **Result:** pass after the corrections recorded below

This is a rigorous author self-review, not an independent qualified avionics, functional-safety, hardware, or flight-readiness review.

## Reviewed invariants

1. Exactly one post-start task owns each driver and complete bus transaction.
2. Interrupts perform only bounded flag, capture, and UART-ring work and call no FreeRTOS API.
3. Every scheduler object and stack has static application storage; no heap implementation is linked.
4. The application receives coherent copies and submits bounded commands instead of touching hardware.
5. The watchdog is refreshed by one highest-priority task only after positive evidence of liveness, status, freshness, deadline, and stack health.
6. All time comparisons remain correct across the 32-bit millisecond counter rollover.
7. Deliberately long radio/BLE maintenance calls cannot disguise a task stall or defer freshness indefinitely.

## Evidence examined

- FreeRTOS Kernel V10.6.2 common sources and Cortex-M7 r0p1 GNU/IAR ports under `ThirdParty/FreeRTOS-Kernel/`.
- SHA-256 comparison of all 36 copied vendor files (excluding the Atlas-authored local README) against `STM32Cube_FW_H7_V1.13.0/Middlewares/Third_Party/FreeRTOS/Source`; every file matched.
- `FreeRTOSConfig.h`: 1 kHz preemption, static-only allocation, FPU enabled, stack checks, priority 15 kernel exceptions, and maximum syscall priority 5.
- `main.c`, `stm32h7xx_it.c`, startup vector behavior, NVIC configuration, and the GNU/IAR port membership.
- All project-owned task, queue, mutex, stream, delay, driver, callback, and watchdog call sites.
- Host tests for ordinary and wrapped periodic/freshness calculations and every supervisor decision branch, including active/expired long operations.

## Findings and corrections

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

## Interrupt and priority conclusion

The project requires `NVIC_PRIORITYGROUP_4`. SysTick and PendSV run at numerical priority 15; the maximum FreeRTOS syscall priority is 5. Existing priority 0–4 callbacks only set flags/counters or copy capture bytes and call no `...FromISR()` API, so they are legal above the kernel masking threshold. UART interrupt priorities are 6 and also call no RTOS API. Any future ISR-to-RTOS call requires a new priority audit.

The project-owned SysTick wrapper advances the HAL millisecond tick and calls the kernel tick only after scheduling starts. SVC and PendSV bodies come from the selected FreeRTOS port, avoiding duplicate handlers.

## Open verification risks

- No assembled Atlas board was available in this review. Sensor rates, bus latency, UART/SHTP backlog recovery, interrupt loading, control jitter, and fault-reset behavior remain unmeasured.
- IWDG timing is derived from generated prescaler/reload settings and the nominal LSI frequency; oscillator tolerance and the real reset interval must be measured on each supported hardware revision.
- Radio/BLE mode transitions are explicitly maintenance-only. The application must inhibit control while `maintenance_active` or `sensor_recovery_active` is true.
- The 64-word live stack threshold and static stack allocations are conservative starting values, not proven worst-case margins.
- Fault context is not retained across reset; reset-cause/crash persistence remains future work.

## Conclusion

The RTOS design is internally coherent and fail-closed at source/policy level after correction of the findings above. It is suitable as an integration foundation, but it is not yet hardware- or flight-validated. Review 5 separately verifies implementation/build evidence; Review 6 audits documentation and repository readiness.
