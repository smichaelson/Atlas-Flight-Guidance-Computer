# Repository Import Notes

This repository was curated on 2026-09-01 from the desktop folder `Atlas`. The source folder was treated as read-only and was not deleted from or modified.

## Included

- Current STM32CubeMX configuration: `Atlas.ioc` and `.mxproject`.
- Current generated firmware and integration code: `Core/`, `FATFS/`, and `USB_DEVICE/`.
- Required middleware: FatFs and STM32 USB Device Library.
- Required STM32H7 HAL/LL and CMSIS/device headers used by the CMake/IAR projects.
- CMake, Ninja preset, GNU startup/linker, and IAR EWARM project files.
- Cross-platform STM32Cube editor/tool-bundle settings from `.settings/`, `.vscode/`, and `.clangd`.
- The ten-page Rev. 0.1 schematic PDF export.
- The generic BOM and the files from `Trinity/Manufacturing Files/` as one candidate manufacturing package.
- New collaborator documentation, standards, repository hygiene files, and pull-request checklist.

## Intentionally excluded

| Source item | Reason |
|---|---|
| `Atlas_Origin.ioc` | Obsolete/origin configuration explicitly excluded by the project owner |
| Raw KiCad design files | Explicitly excluded; none were present in the inspected source tree |
| `build/` | Generated compiler, CMake, Ninja, index, object, and cache output |
| `READ_ME.md` | Its August 14 current-state claims predate the August 15/16 regeneration and are no longer accurate |
| `STM_Implementation.md` | Valuable planning material, but its opening current-state audit also predates the regenerated baseline; current standards were distilled and revalidated instead |
| `SUMMARY.docx` | Replaced by a searchable Markdown hardware overview; the binary source was not needed for builds or collaboration |
| Root `Atlas.pdf` and `Atlas.txt` | Generated CubeMX reports; they duplicate `Atlas.ioc` and expose a contributor's absolute desktop path |
| Root duplicate JLCPCB BOM/CPL files | The BOM duplicates visible data and the CPL differs materially from the manufacturing-folder variant; retaining both without provenance would be ambiguous |
| Unused CMSIS DSP, NN, RTOS, DAP, examples, templates for other cores, and build-system content | Not referenced by the current Atlas CMake or IAR build and would obscure project-owned work |

## Manufacturing-package decision

The two source JLCPCB BOM workbooks contained the same visible 106-row by 4-column data, although the files were not byte-identical. The two CPL CSV files each contained 320 rows but differed in 98 rows, including rotations and some positions.

To avoid presenting conflicting files as equally authoritative, this repository retains the BOM and CPL located alongside the Gerbers and drill files in `Trinity/Manufacturing Files/`. This is a packaging decision, not proof that those exact files were submitted for the manufactured board. See [`../hardware/manufacturing/README.md`](../hardware/manufacturing/README.md).

## Vendor-source curation

The original source folder contained broad CMSIS packages, examples, DSP, NN, RTOS, and debug-access material not referenced by Atlas. The repository keeps:

- `Drivers/CMSIS/Include/`
- `Drivers/CMSIS/Device/ST/STM32H7xx/`
- `Drivers/CMSIS/LICENSE.txt`
- `Drivers/STM32H7xx_HAL_Driver/`

The current build references are validated against this curated set. If a future CubeMX change enables another component, regenerate from the pinned STM32Cube package and add only the required source plus its license/notice files.

