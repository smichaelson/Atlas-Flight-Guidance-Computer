# Hardware Reference Package

This directory contains review and manufacturing exports for the Atlas Flight Computer Rev. 0.1. It does not contain the editable KiCad project.

For contributor-facing pins, power and known discrepancies, use the centralized [hardware reference](../docs/reference/HARDWARE.md). For firmware capability and acceptance, use [Systems](../docs/SYSTEMS.md); this directory is the artifact manifest, not another bring-up guide.

## Files

- `Atlas-schematic-rev-0.1.pdf` - ten-page KiCad schematic PDF export, title-block date 2026-03-19.
- `AtlasBOM.csv` - generic component BOM from the source hardware folder.
- `manufacturing/` - candidate Gerber, drill, JLCPCB BOM, and component-placement package.

## Authority and limitations

The assembled board and confirmed as-built order records override these exports. The schematic is the primary electrical reference in this repository, while the firmware pin labels and `.ioc` configuration are implementation artifacts.

The exact files used for the already manufactured board were not identified in the source folder. Do not reorder from `manufacturing/` without completing the checks in [`manufacturing/README.md`](manufacturing/README.md).

No `.kicad_sch`, `.kicad_pcb`, `.kicad_pro`, footprint library, or symbol library is included. Obtain the canonical editable project from its owner before changing the design. Do not reconstruct an authoritative board from Gerbers.
