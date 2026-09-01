# Atlas Flight Guidance Computer

Atlas is a custom STM32H743-based flight-computer and data-acquisition board. The hardware combines protected power conversion, inertial and environmental sensing, GNSS timing, Bluetooth Low Energy, an external long-range radio interface, microSD storage, USB, actuator outputs, general-purpose I/O, and five physically armed event-output channels.

> [!WARNING]
> This repository is an early engineering baseline, not flight-qualified software. The current firmware is primarily STM32CubeMX-generated initialization code and does not contain a flight state machine, sensor drivers, telemetry protocol, data logger, or validated event-output controller. Do not connect energetic devices while developing or testing this code. Use unarmed hardware and inert dummy loads.

## Current baseline

- **Board:** Atlas Flight Computer, schematic revision 0.1, dated 2026-03-19.
- **MCU:** STM32H743ZIT6, Arm Cortex-M7, LQFP144, 2 MiB flash.
- **Configuration:** STM32CubeMX 6.17.0 with STM32Cube FW_H7 1.13.0.
- **Toolchains:** CMake/Ninja with Arm GNU Toolchain and an IAR EWARM project.
- **Firmware state:** peripheral configuration is generated, but project-owned application logic has not been implemented.
- **Runtime caveat:** `main()` starts the independent watchdog and then enters an empty loop without refreshing it. As checked on 2026-09-01, an unmodified image is therefore expected to reset repeatedly.
- **Hardware sources:** the exported schematic and manufacturing outputs are included under [`hardware/`](hardware/README.md). Editable KiCad source is intentionally not included.

See [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) for the detailed snapshot and known gaps.

## Start here

1. Read [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) to understand what exists and what is still missing.
2. Read [`STANDARDS.md`](STANDARDS.md) before changing firmware, generated configuration, hardware artifacts, or safety-relevant behavior.
3. Review [`docs/HARDWARE_OVERVIEW.md`](docs/HARDWARE_OVERVIEW.md) and the final schematic export before assigning pins or electrical limits.
4. Follow [`docs/BUILDING.md`](docs/BUILDING.md) to configure and build the current baseline.
5. Keep hardware tests unarmed until a reviewed test procedure explicitly authorizes otherwise.

## Repository map

| Path | Purpose |
|---|---|
| `Core/` | STM32CubeMX-generated application entry point, MCU support, interrupts, and HAL setup |
| `FATFS/` | Generated FatFs integration and SD block-device glue |
| `USB_DEVICE/` | Generated USB CDC device integration |
| `Drivers/` | Curated CMSIS and STM32H7 HAL/LL sources used by the current build |
| `Middlewares/` | FatFs and STM32 USB Device middleware used by the current build |
| `cmake/`, `CMakeLists.txt`, `CMakePresets.json` | Arm GNU CMake build |
| `EWARM/` | IAR Embedded Workbench project, linker configurations, and startup file |
| `Atlas.ioc`, `.mxproject` | STM32CubeMX configuration and generation metadata |
| `hardware/` | Schematic export, BOM, and candidate manufacturing package; no editable KiCad project |
| `docs/` | Build guide, hardware overview, current status, and import/provenance notes |
| `STANDARDS.md` | Contribution, firmware, safety, verification, and release standards |

## Build

With CMake 3.22 or newer, Ninja, and an `arm-none-eabi` GNU toolchain on `PATH`:

```text
cmake --preset Debug
cmake --build --preset Debug
```

For IAR, open `EWARM/Project.eww` and select the `Atlas` configuration. Full setup, expected outputs, and regeneration cautions are in [`docs/BUILDING.md`](docs/BUILDING.md).

## Source authority

When sources disagree, stop and resolve the conflict instead of guessing.

1. The assembled board and verified as-built records govern physical hardware.
2. [`hardware/Atlas-schematic-rev-0.1.pdf`](hardware/Atlas-schematic-rev-0.1.pdf) governs intended electrical connectivity for this repository snapshot.
3. The compiled C/assembly, active linker script, and build project govern present runtime behavior.
4. `Atlas.ioc` governs intended generated configuration only after its generated diff is reviewed.
5. Markdown documentation explains the baseline but does not override measured hardware or compiled code.

The files under `hardware/manufacturing/` are preserved as a candidate production package, but their exact relationship to the already manufactured board has not been independently established. Review [`hardware/manufacturing/README.md`](hardware/manufacturing/README.md) before ordering boards.

## Contributing

- Open or link an issue that states the problem, affected hardware, safety impact, and acceptance evidence.
- Keep generated changes separate from project-owned application changes whenever practical.
- Include the exact build and bench-test results in the pull request.
- Require a second qualified reviewer for changes that affect clocks, power, memory placement, interrupts, watchdog behavior, actuators, event outputs, or fault handling.
- Update the documentation in the same change when behavior, pin assignments, dependencies, or validation status changes.

The pull-request checklist in [`.github/pull_request_template.md`](.github/pull_request_template.md) operationalizes these requirements.

## Licensing

No project-level license has been selected yet. Do not assume rights beyond the collaboration permission granted by the project owner. Third-party CMSIS, STM32 HAL, USB, and FatFs source retains its original notices and license terms in the vendored files.
