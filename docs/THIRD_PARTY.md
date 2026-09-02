# Third-party Firmware Provenance

## CEVA SH-2

Atlas vendors the official CEVA SH-2/SHTP C library solely for BNO085 communication and report decoding.

| Field | Value |
|---|---|
| Upstream | <https://github.com/ceva-dsp/sh2> |
| Pinned source commit | `b514b1e2586ddc195e553dac89fc94c637b25298` |
| Local path | `ThirdParty/CEVA/sh2/` |
| License | Apache License 2.0, retained in source headers and `NOTICE.txt` |
| Vendored files | `sh2.c/.h`, `shtp.c/.h`, `sh2_SensorValue.c/.h`, `sh2_util.c/.h`, `sh2_hal.h`, `sh2_err.h` |
| Local modifications | None to upstream source files |

The Atlas adapter is `App/Src/atlas_bno085.c`. It implements only the target-specific HAL required by upstream: reset/open/close, bounded SHTP-over-I2C reads and writes, a 32-bit microsecond timer, interrupt deferral, product-ID verification, and decoded callback forwarding.

## FreeRTOS Kernel

Atlas vendors the minimum FreeRTOS Kernel subset required for static tasks, queues/mutexes, and stream buffers.

| Field | Value |
|---|---|
| Upstream | <https://github.com/FreeRTOS/FreeRTOS-Kernel> |
| Version | V10.6.2 |
| Package source | Local `STM32Cube_FW_H7_V1.13.0/Middlewares/Third_Party/FreeRTOS` installation |
| Local path | `ThirdParty/FreeRTOS-Kernel/` |
| License | MIT, retained in `ThirdParty/FreeRTOS-Kernel/LICENSE` and source headers |
| Common sources | `tasks.c`, `queue.c`, `list.c`, `stream_buffer.c`, and public `include/` headers |
| Target ports | GNU and IAR `ARM_CM7/r0p1`; IAR also uses `portasm.s` |
| Excluded by design | Heap implementations, CMSIS-RTOS wrappers, event groups, co-routines, and software timers |
| Local modifications | None to copied upstream/ST kernel files |

Project configuration is outside the vendor tree in `App/Inc/FreeRTOSConfig.h`; integration is in `App/Src/atlas_rtos.c`, `atlas_rtos_policy.c`, `atlas_time.c`, and the generated-file user sections. The r0p1 Cortex-M7 port is selected because it includes the documented erratum workaround and is safe when the exact processor revision has not yet been physically confirmed.

## CEVA update procedure

1. Review upstream release notes and all commits between the pinned and candidate versions.
2. Replace files without removing upstream headers or notices.
3. Record the new immutable commit and the exact file set here.
4. Rebuild Debug and Release with both supported project configurations.
5. Run host tests and the complete BNO085 bench procedure, including reset, partial/invalid SHTP transfers, report configuration, and interrupt recovery.
6. Retain the source diff and license review with the pull request.

Do not edit third-party code to hide an integration problem. Fix the Atlas adapter or carry a minimal, documented patch with a reproducible upstream reference.

## FreeRTOS update procedure

1. Review FreeRTOS security/release notes, the STM32CubeH7 integration delta, and every kernel/port change from V10.6.2.
2. Select one immutable version compatible with both supported toolchains and the STM32H743 FPU/errata state.
3. Replace the reviewed subset without editing vendor files or dropping the MIT license/history.
4. Reconcile every `FreeRTOSConfig.h` option; do not silently enable dynamic allocation, timers, tickless idle, or a CMSIS wrapper.
5. Build GNU Debug/Release and IAR from scratch. Verify handler symbols, SysTick/HAL integration, FPU port, no heap symbol, memory placement, and stack-usage output.
6. Run host policy/protocol tests and all RTOS bench/fault-injection tests, including task stalls, queue/stream capacity, long-operation deadlines, and measured stack margins.
7. Update the version/provenance, build sizes, validation ledger, and three RTOS review records in the same pull request.
