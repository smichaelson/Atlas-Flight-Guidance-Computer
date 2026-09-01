# Project Status

- **Baseline reviewed:** 2026-09-01
- **Imported source snapshot:** local desktop folder `Atlas`
- **Hardware reference:** Atlas Flight Computer schematic Rev. 0.1, dated 2026-03-19

This file describes what is demonstrably present in the repository. It is not a roadmap commitment or flight-qualification statement.

## What exists

- STM32CubeMX configuration for an STM32H743ZIT6 using CubeMX 6.17.0 and STM32Cube FW_H7 1.13.0.
- Generated startup and initialization for GPIO, DMA, ADC1/ADC3, I2C1/I2C2, SPI2/SPI3, UART4, USART1/USART3/USART6, timers, RTC, independent watchdog, RAM ECC monitoring, SDMMC, FatFs, and USB CDC support files.
- CMake/Ninja support for Arm GNU Toolchain builds.
- An IAR EWARM workspace and linker configurations.
- A final Rev. 0.1 schematic export, generic BOM, and one candidate Gerber/drill/assembly package.
- Safe initial GPIO values in the generated setup for the five event-output gates (low), sensor/external SPI chip selects (high), and module reset pins (held low during initial setup).

## What does not yet exist

- Flight or mission state machine.
- Project-owned board-support layer and peripheral drivers.
- Sensor acquisition, calibration, frame transforms, or fusion.
- GNSS parsing and time discipline.
- RFD900x or BLE application protocols.
- A durable microSD log format and power-loss recovery policy.
- A USB CDC command/security policy.
- Actuator control profiles or validated PWM behavior.
- A reviewed event-output arming/firing state machine and bounded pulse controller.
- Health supervisor, fault manager, reset diagnostics, or operational watchdog policy.
- Unit tests, hardware-in-the-loop tests, continuous integration, or release automation.
- A project-level license.

## Current runtime behavior

The generated `main()` performs MCU, clock, GPIO, DMA, and most peripheral initialization, then enters an empty infinite loop. The following details are important:

1. `MX_IWDG1_Init()` is called with prescaler 64 and reload 999.
2. No application-level invocation of `HAL_IWDG_Refresh()` exists in the repository.
3. The unmodified image is therefore expected to reset repeatedly once the watchdog expires.
4. `MX_FATFS_Init()` is called, but `MX_SDMMC1_SD_Init()` is not called from `main()`.
5. USB CDC support is generated, but `MX_USB_DEVICE_Init()` is not called from `main()`.
6. Peripheral initialization is not the same as operational functionality: timers, ADC conversions, communications, storage, sensors, and outputs are not started or managed by application code.

This behavior is suitable only as a configuration snapshot. Do not treat it as a stable board-support package.

## Current clock baseline

The current `.ioc` and generated clock setup select:

| Domain | Configured value |
|---|---:|
| HSE | 8 MHz |
| LSE | 32.768 kHz |
| CPU / SYSCLK | 200 MHz |
| AXI / HCLK | 100 MHz |
| APB1/APB2/APB3/APB4 | 50 MHz |
| General APB timer kernel | 100 MHz with the configured prescalers |
| PLL2 peripheral outputs | 50 MHz |
| USB kernel | 48 MHz from PLL3 |

Any clock-tree change must be reviewed against `STANDARDS.md` and verified in the generated source, not only in the CubeMX graphical view.

## Hardware status and provenance

- The schematic title block identifies Rev. 0.1.
- The Gerber job identifies a six-layer board approximately 70.05 mm by 140.05 mm with approximately 1.596 mm nominal thickness.
- The source folder states that the design has been manufactured, but the exact submitted JLCPCB order archive was not present.
- Two source CPL files had 320 rows each but differed in 98 rows. Only the variant located in the source folder's `Trinity/Manufacturing Files/` directory was retained here, because it forms a coherent package with the Gerbers and drills. It is not independently confirmed as the as-built CPL.
- The two source JLCPCB BOM workbooks were byte-different but contained the same visible 106-by-4 BOM data. The manufacturing-folder copy was retained.
- Editable KiCad source was not present in the inspected source tree and is not included in this repository.

Do not place a repeat manufacturing order until the project owner identifies the exact as-built package and a reviewer checks polarity, orientation, substitutions, stack-up, and assembly options.

## Immediate priorities

1. Select a project license and record repository ownership/contribution terms.
2. Establish a reproducible clean build in continuous integration for Debug and Release.
3. Create a project-owned board-support layer with explicit safe-output and reset/fault handling.
4. Define a watchdog health-supervision policy before refreshing the watchdog.
5. Bring up one subsystem at a time on current-limited, unarmed hardware and retain test evidence.
6. Identify the exact as-built manufacturing package and board revision.
7. Define requirements, acceptance tests, and traceability before implementing flight or event-output logic.

## Updating this status

Change this file in the same pull request that changes an implemented capability, toolchain baseline, known safety limitation, hardware revision, or manufacturing provenance. Claims should cite repository paths or retained test evidence in the pull-request description.
