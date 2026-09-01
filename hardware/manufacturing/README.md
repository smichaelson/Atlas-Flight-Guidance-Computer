# Candidate Manufacturing Package

These files were copied together from the source folder's `Trinity/Manufacturing Files/` directory. They are preserved as a coherent candidate package, but they have not been independently confirmed as the exact package submitted for the already manufactured board.

## Contents

- Six copper layers: `F_Cu`, `In1_Cu` through `In4_Cu`, and `B_Cu`.
- Front/back solder mask and silkscreen.
- Front/back paste layers.
- Edge cuts.
- Plated and non-plated drill files.
- KiCad Gerber job metadata.
- JLCPCB BOM workbook.
- JLCPCB component-placement CSV.
- KiCad all-positions CSV.
- `SHA256SUMS.txt` for checking that the preserved package has not changed.

## Before fabrication or assembly

1. Identify and archive the exact files and assembly options used for the as-built board, if available.
2. Confirm schematic, BOM, CPL, Gerbers, drill files, stack-up, dimensions, and board revision all describe the same design.
3. View every Gerber layer and both drill files in an independent viewer.
4. Check connector orientation, pin 1, polarity, and rotation for every IC, diode, LED, polarized capacitor, module, and bottom-side component.
5. Reconcile the JLCPCB BOM against current availability and approved substitutions.
6. Reconcile the CPL against the assembled reference board or a reviewed 3D/placement view.
7. Confirm panelization, controlled impedance, copper weight, finish, thickness, and assembly-side options.
8. Record reviewer names, date, package hash, and the final order identifier.

Verify the preserved files from this directory with a SHA-256 tool and compare the result with `SHA256SUMS.txt` before review or ordering.

The source tree also contained a second CPL with the same row count but 98 differing rows. It is intentionally not included here. Do not substitute another CPL by filename alone.

