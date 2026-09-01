# Building Atlas Firmware

The repository contains two project configurations: an Arm GNU CMake build and an IAR Embedded Workbench project. Building proves that the source is internally consistent; it does not prove that the image is safe or operational on hardware.

## Arm GNU and CMake

### Prerequisites

- CMake 3.22 or newer.
- Ninja.
- Arm GNU Toolchain with `arm-none-eabi-gcc`, `arm-none-eabi-g++`, `arm-none-eabi-objcopy`, and `arm-none-eabi-size` on `PATH`.
- A clean checkout path without generated files copied from another machine.

The source snapshot was generated with the STM32Cube tool bundle using Arm GNU Toolchain 14.3.1, CMake 4.3.1, and Ninja 1.13.2. Later compatible versions may work, but a version change should be recorded with the build evidence.

### Curated-baseline verification

On 2026-09-01, clean Debug and Release builds both linked successfully with Arm GNU 14.3.1, CMake 4.3.1, and Ninja 1.13.2. The build reported one warning in both configurations: `MX_SDMMC1_SD_Init()` is generated but unused. This agrees with the current-status note that SDMMC initialization is not called from `main()`.

| Configuration | Flash used | DTCM used | Result |
|---|---:|---:|---|
| Debug | 84,412 bytes | 19,992 bytes | Linked; one unused-function warning |
| Release | 41,804 bytes | 19,992 bytes | Linked; one unused-function warning |

The IAR project was preserved and inspected for source membership, but an IAR build was not executed during repository curation.

### Debug build

From the repository root:

```text
cmake --preset Debug
cmake --build --preset Debug
```

### Release build

```text
cmake --preset Release
cmake --build --preset Release
```

The presets place generated build state under `build/Debug` and `build/Release`. Those directories are intentionally ignored by Git.

For every pull request that changes compiled behavior, retain the compiler version, warning output, memory-usage summary, and map-file review in the pull-request evidence.

## IAR Embedded Workbench

1. Open `EWARM/Project.eww`.
2. Select the `Atlas` configuration.
3. Confirm the active linker configuration before building.
4. Rebuild the entire project rather than relying on objects copied from another machine.
5. Review the generated map for flash/RAM placement, stack, heap, and any DMA-owned buffers.

The repository carries several IAR linker files. Their presence does not make them interchangeable. The project-selected linker file governs the image; changing it requires a memory-placement review and bench validation.

## STM32CubeMX regeneration

Open `Atlas.ioc` only after reading the regeneration procedure in [`../STANDARDS.md`](../STANDARDS.md). Use STM32CubeMX 6.17.0 with STM32Cube FW_H7 1.13.0 for ordinary regeneration. Generate into a comparison copy first and inspect the complete diff.

Do not copy the local `build/` directory, CubeMX reports containing machine-specific paths, or editor caches into the repository.

## Programming and hardware bring-up

- Use SWD through the debug connector and begin with a current-limited supply.
- Keep the physical event arm link open and attach only inert dummy loads.
- Expect the current unmodified image to reset because the independent watchdog is initialized but not refreshed.
- Do not work around the watchdog by adding an unconditional refresh. Implement and test a health-supervision policy first.
- Verify boot GPIO levels and supply rails before starting timers, communications, or external loads.

## Troubleshooting

- **Compiler not found:** confirm the Arm GNU `bin` directory is on `PATH` before running CMake.
- **Stale compiler path:** remove only the local `build/<preset>` directory and reconfigure; never commit the generated cache.
- **Generated source changed unexpectedly:** stop, compare the CubeMX version/package, and review `Atlas.ioc` plus the full generated diff.
- **Build succeeds but hardware resets:** the current watchdog behavior is documented in [`PROJECT_STATUS.md`](PROJECT_STATUS.md).
- **One toolchain builds and the other fails:** check source membership, compiler definitions, assembly startup file, linker script, and C-language extensions in both projects.
