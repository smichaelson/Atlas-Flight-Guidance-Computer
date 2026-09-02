# Atlas Flight Guidance Computer

Atlas is a collaborative STM32H743 flight-computer development project for the Rev. 0.1 board. It provides sensor and communications drivers, a statically allocated FreeRTOS framework, owned peripheral services, build projects, and reviewed hardware exports.

**Current status: software integration implemented; hardware qualification outstanding.** The reviewed startup, timing, GNSS, SD, USB and output-service defects have corrections and regression tests. No board, radio link, actuator or pyro load was exercised. Passing builds and host models do not establish electrical operation or flight readiness.

## Start here

**PCB on the bench? Start with [startup](docs/startup.md)** — the complete power/BOOT0/reset/USB-programming procedure, local dashboard and staged tests. Build the **Bringup** preset, not the normal Debug/Release application. Check the documented USB schematic/manufacturing polarity conflict against the actual board before USB use.

For code collaboration, read [Quick start](docs/QUICK_START.md) and [Systems](docs/SYSTEMS.md): what runs, how to build/check it, and what remains unverified.

For implementation, use [Peripheral services](docs/PERIPHERALS.md) and [Development and engineering standards](docs/DEVELOPMENT.md). The [documentation hub](docs/README.md) indexes optional device, RTOS and hardware references. The [current review report](docs/REVIEW_REPORT.md) records corrections, three review passes, evidence and remaining release gates.

## Repository map

| Location | Purpose |
|---|---|
| [App](App/) | Drivers, RTOS ownership, typed snapshots, copied commands and safety policy |
| [Core](Core/), [FATFS](FATFS/), [USB_DEVICE](USB_DEVICE/) | Generated setup plus maintained board/filesystem/USB integration |
| [Tests](Tests/) | Protocol, integration, service-model and repository checks |
| [tools/bringup](tools/bringup/) | Offline-capable desktop dashboard, image verification and safe SD fixtures |
| [docs](docs/README.md) | Central documentation; references are optional, not a reading chain |
| [hardware](hardware/README.md) | Schematic PDF, BOM and candidate manufacturing exports |
| [ThirdParty](ThirdParty/), [Drivers](Drivers/), [Middlewares](Middlewares/) | Pinned dependencies; local USB patches are documented in provenance |
| [CMakeLists.txt](CMakeLists.txt), [EWARM](EWARM/) | Arm GNU and IAR project definitions |
| [Atlas.ioc](Atlas.ioc) | Generator inputs; runtime overrides and manual integrations must survive regeneration |

J9's intended radio is **RFD900x**, confirmed by the owner. “LoRa” is a legacy connector label, not a Semtech LoRa protocol selection.

## Safety and provenance

All PWM, general GPIO and pyro outputs start disabled/low. The isolated **Bringup** image never enables PWM/pyro or calls the flight hook; it permits only explicit, one-second logic-GPIO tests and indicator/link/media actions. In the normal application, pyro arming and PWM operation require explicit qualified configuration; no flight decisions or remote firing parser are provided. The specified pyro policy is a 500 ms pulse, at least 500 ms OFF, and at most three retries: **four attempts per channel per boot**. Re-arming does not refill that budget.

Keep the physical pyro arm link open and disconnect energetic loads and servos during ordinary development. J5's armed feed bypasses the main eFuse. Use current-limited bench power; USB does not power the system rails. Output-low is an electrical state, not a guaranteed safe mechanical servo position.

Raw KiCad projects and obsolete origin documents remain excluded. The separate original `Atlas` folder is untouched. Hardware exports are references, not assembled-board or fabrication approval.

No project-level license has been selected; vendor licenses remain applicable. See [provenance](docs/reference/PROVENANCE.md).
