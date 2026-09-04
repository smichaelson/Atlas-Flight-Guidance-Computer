# Atlas documentation

## Choose the question, not a reading list

| I need to… | Read |
|---|---|
| Power up the new PCB, program over USB and run the dashboard/tests | **[startup](startup.md)** — one complete bench procedure |
| Understand the project and run its first checks | [Quick start](QUICK_START.md) |
| Know whether a subsystem is usable and how to accept it | [Systems and readiness](SYSTEMS.md) |
| Use SD, USB, ADC, GPIO, PWM, pyro, expansion or commissioning APIs | [Peripheral services](PERIPHERALS.md) |
| Build, test, add code, or review a change | [Development and engineering standards](DEVELOPMENT.md) |
| Understand a reported defect or the latest review evidence | [Current review report](REVIEW_REPORT.md) |
| Work on task ownership, timing, snapshots, or watchdogs | [RTOS reference](reference/RTOS.md) |
| Trace connectors, rails, pin assignments, or hardware conflicts | [Hardware reference](reference/HARDWARE.md) |
| Check vendor versions, licenses, or import decisions | [Provenance](reference/PROVENANCE.md) |

New collaborators normally need only **Quick start + Systems**. Bench operators use **startup** as their single procedure. References below are for the device or layer you are changing, not prerequisites to reading the application code.

## Device references

| Motion and environment | Navigation and links | Feedback |
|---|---|---|
| [ADXL375 high-g accelerometer](reference/modules/ADXL375.md) | [NEO-M9N GNSS / GPS](reference/modules/GNSS.md) | [RGB LED — hardware inhibited](reference/modules/LED.md) |
| [LSM6DSV16B IMU](reference/modules/LSM6DSV16B.md) | [NINA-B112 BLE](reference/modules/BLE.md) | [Differential buzzer](reference/modules/BUZZER.md) |
| [MMC5983MA magnetometer](reference/modules/MMC5983MA.md) | [RFD900x radio — J9 “LoRa” label](reference/modules/RFD900X.md) | |
| [MS5611 barometer](reference/modules/MS5611.md) | | |
| [BNO085 sensor hub](reference/modules/BNO085.md) | | |

SD, USB, ADC, PWM, GPIO, pyro and expansion now have owned services. Their commands, data units, result lifetime and qualification rules are centralized in [Peripheral services](PERIPHERALS.md). [Systems](SYSTEMS.md) distinguishes implementation from physical acceptance.

## How these documents stay authoritative

- **Systems** owns current capability and acceptance status. The **review report** owns current finding IDs and review evidence.
- **startup** owns the staged bench procedure, BOOT/reset/USB programming, dashboard use, fixtures and diagnostic wire protocol. Do not duplicate those procedures in device guides.
- **Development** owns engineering standards and build/test commands. **Peripheral services** owns the new board-service API contracts; **RTOS** owns scheduling and cross-owner rules. Per-device guides own protocol details.
- **Hardware** summarizes the schematic but never overrides as-built measurements, exact-part specifications, or unresolved discrepancies.
- [Earlier reviews](archive/REVIEW_HISTORY.md) are historical evidence only. Their “pass” conclusions and pre-correction failures do not supersede the current report.

This layout replaces the scattered status, validation, bring-up, building, architecture, standards, and review-index entry points. Relevant detail is retained in the references and a single historical archive; new reviews should update the current report rather than create another mandatory reading chain.
