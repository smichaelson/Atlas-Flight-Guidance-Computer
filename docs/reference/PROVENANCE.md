# Provenance, dependencies and retained history

This reference combines import history and dependency policy. Current capability is in [Systems](../SYSTEMS.md); engineering rules are in [Development](../DEVELOPMENT.md).

## Dependency baseline

| Component | Source / version | Local scope |
|---|---|---|
| STM32 generator | STM32CubeMX 6.17.0; STM32Cube FW_H7 1.13.0 | `Atlas.ioc`, generated configuration, HAL/CMSIS, FatFs and USB middleware |
| FreeRTOS Kernel | V10.6.2 from the locally installed STM32CubeH7 1.13.0 package; [upstream](https://github.com/FreeRTOS/FreeRTOS-Kernel) | [Kernel subset](../../ThirdParty/FreeRTOS-Kernel/README.md) |
| CEVA SH-2/SHTP | [Upstream commit b514b1e2586ddc195e553dac89fc94c637b25298](https://github.com/ceva-dsp/sh2/tree/b514b1e2586ddc195e553dac89fc94c637b25298) | [BNO085 protocol library](../../ThirdParty/CEVA/sh2/README.md) |
| Laptop bring-up transport | [pyserial 3.5 documentation](https://pyserial.readthedocs.io/en/latest/pyserial_api.html); pinned in [requirements](../../tools/bringup/requirements.txt) | Optional local Python virtual environment, not vendored or linked into MCU firmware; retain distribution notices |

FreeRTOS retains its [MIT license](../../ThirdParty/FreeRTOS-Kernel/LICENSE). CEVA retains Apache-2.0 notices in source headers and [NOTICE.txt](../../ThirdParty/CEVA/sh2/NOTICE.txt). Preserve all vendor notices and component-specific licenses; the repository has **no selected project-level license**.

The kernel subset includes `tasks.c`, `queue.c`, `list.c`, `stream_buffer.c`, public headers, and GNU/IAR Cortex-M7 r0p1 ports (IAR also uses `portasm.s`). It excludes heap implementations, CMSIS wrappers, software timers, event groups and co-routines. Project configuration is in [FreeRTOSConfig.h](../../App/Inc/FreeRTOSConfig.h), not the vendor tree. The selected M7 port contains the relevant port workaround; fitted silicon revision and errata still require physical confirmation.

The CEVA adapter in [atlas_bno085.c](../../App/Src/atlas_bno085.c) supplies reset/open/close, bounded I2C SHTP reads/writes, interrupt deferral, product identity and microsecond timing. The pinned library includes `sh2`, `shtp`, `sh2_SensorValue`, `sh2_util`, `sh2_hal` and `sh2_err` sources/headers.

### Preserved baseline comparisons

The preceding provenance review found that all 31 vendored kernel C/header/assembly files matched the installed package after **CRLF/LF normalization**; they were not byte-identical to that local package. Before the 2026-09-03 board follow-up, all 12 retained CEVA files were byte-identical to the clean local checkout at the pinned commit. The current tree intentionally changes only [`sh2.c`](../../ThirdParty/CEVA/sh2/sh2.c) in that set: `getProdIdOp` has a five-second timeout so a missing product-ID response cannot trap pre-scheduler initialization forever. The other 11 CEVA files remain at the pinned baseline. These checks establish source provenance, not correctness, security currency, or compatibility with untested hardware.

All 19 entries in the [manufacturing SHA-256 manifest](../../hardware/manufacturing/SHA256SUMS.txt) matched. Hash agreement establishes preservation, not manufacturing correctness.

## Local integration changes (2026-09-02 through 2026-09-04)

The correction work does not upgrade HAL/CMSIS, FreeRTOS, CEVA, FatFs or the USB library version. It carries these explicit, reviewable local USB middleware changes:

| Vendor path | Local change and reason |
|---|---|
| [CDC class](../../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c) | Validate supported single-ACM request direction/interface/length, reject short OUT payloads, propagate control failure, retain EP0 status data past return, initialize pending opcode and propagate rejected TX launch |
| [USB core](../../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c) | Do not send a status ACK after an EP0 class callback rejects its payload |
| [USB definitions](../../Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h) | Make endpoint status 16-bit, matching the two-byte USB GET_STATUS transfer rather than leaking the adjacent is-used byte |

These are Atlas single-CDC-profile fixes against the bundled source, not an upstream release or a general composite-class redesign. Preserve the ST license headers. [The real core/class regression test](../../Tests/services/test_usb_cdc.c) covers the corrected request/data paths. Reconcile each change explicitly on a vendor update; do not overwrite it with an unreviewed package refresh.

The sole CEVA-source patch is the `getProdIdOp.timeout_us = 5000000` initializer described above. It does not change packet parsing or wire data. [`test_sh2_timeout.c`](../../Tests/host/test_sh2_timeout.c) compiles the actual pinned `sh2.c`, `shtp.c`, and `sh2_util.c` and proves a silent accepted request returns `SH2_ERR_TIMEOUT`. [`test_bno085_ceva.c`](../../Tests/host/test_bno085_ceva.c) runs the project adapter with the actual parser through reset, four product records and four feature writes; the focused mock additionally checks the held-LOW edge gate, maximum transfer, exact shifted address and reset-first shared-I2C recovery. Preserve the Apache-2.0 header and re-evaluate this small patch when advancing CEVA.

Generated/project integration also changes: SD BSP/disk becomes polling and explicit-mount-generation-owned (including ST's disk initialization cache); USB PCD receives a dedicated EP2 TX FIFO and VBUS lifecycle; ADC/TIM6/PWM runtime overrides, fatal/ADC/ECC callbacks and DMA placement are maintained project contracts. The [regeneration instructions](../DEVELOPMENT.md#generated-code-and-dependencies) and [peripheral guide](../PERIPHERALS.md#verification-and-regeneration) list them.

The correction review also made main-stack placement explicit in IAR and added GNU assertions against MPU-guard overlap. IAR placement syntax/section precedence was checked against [IAR's linker documentation](https://docs.iar.com/ewarm/10.1x/en/iar-c-c---development/linking-using-ilink/placing-code-and-data-the-linker-configuration-file.html); an actual IAR build remains pending. GNU's generated `sysmem.c` allocator was replaced with a rejecting zero-heap boundary, and radio/BLE command formatting no longer depends on libc formatted I/O. No C-library source was modified.

The obsolete weak-callback SD host wrapper was retired after replacing the DMA callback model with tests compiling the actual polling BSP/disk and real FatFs. Its original remains in the pre-correction backup; it is not part of current acceptance.

### PCB bring-up additions

The subsequent PCB bring-up work adds an isolated compile-time application, strict USB diagnostic protocol, desktop Tk dashboard, exclusive SD fixture test, offline image verifier and `Bringup` presets. It changes no vendor-library version or manufacturing file; the later bounded CEVA operation-table patch is disclosed above. Normal and diagnostic output gates are distinct; the bring-up protocol cannot enable PWM/pyro or save module configuration. The board's factory STM32 ROM handles DFU; no application bootloader binary was imported. Python/Tk and pyserial are laptop-side only; no web service or account is involved. See [startup](../startup.md) for use and [review evidence](../REVIEW_REPORT.md) for boundaries.

## Dependency updates

1. Review immutable candidate revisions, release/security notes, ST integration deltas and exact processor/toolchain compatibility.
2. Preserve licenses and record the precise file set/version. Do not silently modify vendor sources to hide an adapter defect; any necessary patch needs a minimal retained diff and upstream reference.
3. Reconcile configuration explicitly. Do not enable a heap, second RTOS wrapper, timers or tickless idle as an incidental update.
4. Rebuild GNU Debug/Release and IAR; review handler symbols, FPU/port choice, memory regions, stacks, compiler diagnostics and absence of unintended allocation.
5. Repeat protocol and review probes plus affected inert bench tests. Kernel changes require scheduling/IRQ/overflow/stall/deadline/stack tests; CEVA changes require SHTP/reset/partial-transfer/report/recovery tests.
6. Update this reference, [Systems](../SYSTEMS.md), and the [current review report](../REVIEW_REPORT.md), retaining three distinct review passes for broad integration changes.

## Original import

The original repository was curated on 2026-09-01 from the separate desktop `Atlas` folder, which was treated as read-only. This review worked only on the repository and a staging copy; it did not revisit, delete from or modify that source folder.

Retained material includes current `Atlas.ioc`/`.mxproject`, generated firmware, required HAL/CMSIS/FatFs/USB sources, GNU/IAR build files, portable editor settings, the ten-page Rev. 0.1 schematic, the generic BOM, and one candidate manufacturing package.

| Excluded source material | Reason recorded at import |
|---|---|
| `Atlas_Origin.ioc` / obsolete origin material | Owner explicitly excluded obsolete configuration/documents |
| Raw KiCad design/library files | Explicitly excluded; editable design remains with owner |
| Build/cache/index/object outputs | Generated, machine-specific, not collaboration sources |
| Old `READ_ME.md` and `STM_Implementation.md` | Current-state claims predated regeneration; useful standards were revalidated rather than importing stale audits |
| `SUMMARY.docx` | Replaced by searchable Markdown hardware guidance |
| Generated `Atlas.pdf` / `Atlas.txt` CubeMX reports | Duplicated `Atlas.ioc` and exposed a contributor's absolute path |
| Duplicate root JLCPCB BOM/CPL | Conflicting placement variants would imply false authority |
| Unused CMSIS DSP/NN/RTOS/DAP/examples/other-core templates | Not referenced by the retained build |

The retained CMSIS subset is `Drivers/CMSIS/Include/`, `Drivers/CMSIS/Device/ST/STM32H7xx/`, and its license, alongside the STM32H7 HAL/LL driver tree. Add a newly required vendor component only through a reviewed regeneration/dependency change.

### Manufacturing-package choice

The import record reports two BOM workbooks with the same visible 106-row × 4-column data, and two 320-row placement CSVs differing in 98 rows (rotations and some positions). This review preserved that historical observation; it did not repeat a workbook or fabrication audit.

The package alongside the Gerbers/drills in the source `Trinity/Manufacturing Files/` directory was retained as a coherent **candidate**, not proven as-built order data. See the [manufacturing manifest and ordering prerequisites](../../hardware/manufacturing/README.md). Do not reorder or substitute a CPL by filename alone.

## Documentation migration

The current consolidation replaces the former root standards and separate building/status/validation/bring-up/architecture entry points:

| Former material | Canonical destination |
|---|---|
| `STANDARDS.md`, `docs/BUILDING.md` | [Development](../DEVELOPMENT.md) |
| `PROJECT_STATUS.md`, `VALIDATION.md`, `BRINGUP.md` | [Systems](../SYSTEMS.md), with current defect/test detail in [Review report](../REVIEW_REPORT.md) |
| `FIRMWARE_ARCHITECTURE.md`, `RTOS_ARCHITECTURE.md` | [RTOS reference](RTOS.md) |
| `HARDWARE_OVERVIEW.md` | [Hardware reference](HARDWARE.md) |
| `THIRD_PARTY.md`, `IMPORT_NOTES.md` | This reference |
| `docs/modules/` | [Optional device references](../README.md#device-references) in `reference/modules/` |
| Six separate reviews / review index | [Single historical archive](../archive/REVIEW_HISTORY.md) |

The six earlier reviews and the complete pre-correction codebase review are retained as historical records, with supersession warnings and rebased links. Their old pass conclusions and build figures are not current acceptance. The complete pre-consolidation tree remains recoverable from Git commit `0dbc245`; a separate baseline archive was also retained outside the repository during this work.
