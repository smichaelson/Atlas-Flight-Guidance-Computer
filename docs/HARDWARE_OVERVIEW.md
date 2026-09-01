# Hardware Overview

This is a collaborator-oriented summary of the Atlas Flight Computer Rev. 0.1 electrical design. The physical as-built board and [`../hardware/Atlas-schematic-rev-0.1.pdf`](../hardware/Atlas-schematic-rev-0.1.pdf) remain authoritative when this summary is incomplete or conflicts with measured hardware.

## System at a glance

| Domain | Installed capability |
|---|---|
| Processing | STM32H743ZIT6 Arm Cortex-M7 MCU with 2 MiB flash and distributed SRAM |
| Input power | XT60 input, TPS16850 eFuse/hot-swap stage, and protected `VIN_PROT` rail |
| Regulated rails | `8V4_PWM`, `5V_SYS`, `3V3_SYS`, and ferrite-filtered `3V3_AUX` |
| Motion and environment | BNO085, LSM6DSV16BTR, ADXL375, MMC5983MA, and MS5611 |
| Navigation | u-blox NEO-M9N GNSS with 1 PPS input capture and active-antenna feed |
| Wireless | u-blox NINA-B112 BLE and a dedicated power/UART header for an external RFD900x |
| Storage and service | Four-bit microSD, USB-C full-speed device, SWD, UART, I2C, and SPI |
| Outputs | Eight powered PWM headers, seven general outputs, RGB status LED, and differential buzzer |
| Event channels | Five physically armed, low-side switched channels with continuity and armed-voltage sensing |
| PCB | Six copper layers, approximately 70.05 mm by 140.05 mm |

## Power architecture

Power enters at J1 as `VIN_RAW`. U11, a TPS16850, produces `VIN_PROT`, which feeds three buck converters:

| Rail | Converter | Nominal voltage | Principal loads |
|---|---|---:|---|
| `8V4_PWM` | MP8772 | 8.4 V | Eight PWM power pins |
| `5V_SYS` | LM76003 | 5.0 V | RFD900x header, protected 5 V ports, RGB LED, other 5 V loads |
| `3V3_SYS` | LM76002 | 3.3 V | MCU digital domain, sensors, storage, buses, protected 3.3 V ports |
| `3V3_AUX` | Ferrite-filtered branch | 3.3 V | MCU analog supply, GNSS, BLE, low-noise loads |

The schematic notes an undercharged four-cell source and an explicit 16.3 V maximum at the input. A fully charged nominal 4S lithium pack can exceed that number. Treat 16.3 V as the present system boundary until the hardware owner publishes and validates a complete input-power specification. The approximately 18.7 V eFuse overvoltage threshold is a fault cutoff, not an operating allowance.

The input path has no separate reverse-polarity protection stage ahead of `VIN_RAW`. Observe XT60 polarity and begin bench work with a current-limited supply.

## Sensors

| Device | Role | Interface |
|---|---|---|
| BNO085 (U12) | Fused orientation and nine-axis motion output | I2C1, reset, interrupt |
| LSM6DSV16BTR (U27) | Six-axis accelerometer and gyroscope | SPI3, INT1 to MCU, INT2 at TP11 |
| ADXL375BCCZ (U1) | High-g acceleration | SPI2 with board-specific MOSI gating |
| MMC5983MA (U9) | Three-axis magnetic field | SPI2 |
| MS561101BA03-50 (U14) | Barometric pressure and temperature | I2C1 |

Sensor coordinate frames have not been established in the firmware. Derive and bench-verify a versioned transform for each fitted sensor before navigation or control use.

## Navigation, communications, and storage

- **GNSS:** NEO-M9N on `3V3_AUX`, UART data, and `TIMEPULSE` routed to a timer input-capture pin. J27 is the external SMA antenna path.
- **BLE:** NINA-B112-05B with UART hardware-flow-control signals, module reset/switch lines, and test points.
- **Long-range radio:** J9 provides direct `5V_SYS`, ground, and 3.3 V UART for an external RFD900x. It is not behind either 0.53 A accessory-port limiter; account for roughly 1 A peak transmit demand at maximum radio power.
- **microSD:** Four-bit SDMMC socket with card detect and ESD protection.
- **USB-C:** USB 2.0 full-speed device connection with CC pull-downs, VBUS sensing divider, and ESD protection.
- **Expansion:** External UART, I2C2, SPI3, SWD/reset, seven general inputs, and seven general outputs.

## Powered and event outputs

- Eight PWM headers share `8V4_PWM`; there is no independent electronic current limiter per channel.
- Two 5 V accessory ports and two 3.3 V accessory ports use individual TPS2553 current limiters.
- Five event channels use a physical arm link, common positive bus, and individual low-side MOSFET return switches.
- Each event gate has a series resistor and pull-down. Armed-bus voltage and each channel's continuity node are routed to ADC inputs.
- No independent per-channel fuse or constant-current stage is shown in the event paths. Pulse current is bounded by the source, shared input protection, wiring, copper, MOSFET, connector, load, and pulse duration.

The presence of physical arming and gate pull-downs does not make the current firmware safe to fire loads. Event-output behavior requires a reviewed state machine, bounded hardware-backed pulse duration, fault handling, and test evidence using inert loads before any energetic test is considered.

## Analog monitoring

| Measured node | MCU input | Nominal divider scale |
|---|---|---:|
| `3V3_SYS` | PA0 / ADC1_IN16 | input voltage x 2.000 |
| `8V4_PWM` | PC0 / ADC1_IN10 | input voltage x 3.105 |
| `5V_SYS` | PC1 / ADC1_IN11 | input voltage x 2.000 |
| `VIN_PROT` | PF11 / ADC1_IN2 | input voltage x 9.133 |
| `PYRO_VBAT_ARMED` | PA1 ADC path | 820 kOhm / 100 kOhm divider |
| Continuity 1-5 | PA2, PA3, PA4, PA6, PC4 | 470 kOhm feed, 100 kOhm to ground, 10 nF filter |

Calibrate divider ratios, ADC reference, offsets, and thresholds on assembled hardware. Do not use the nominal values as flight acceptance limits without measurement uncertainty and test evidence.

## Hardware references in this repository

- [`../hardware/Atlas-schematic-rev-0.1.pdf`](../hardware/Atlas-schematic-rev-0.1.pdf): ten-page KiCad PDF export, Rev. 0.1.
- [`../hardware/AtlasBOM.csv`](../hardware/AtlasBOM.csv): generic component BOM.
- [`../hardware/manufacturing/`](../hardware/manufacturing/README.md): candidate Gerber, drill, JLCPCB BOM, and placement package.

Raw `.kicad_sch`, `.kicad_pcb`, and `.kicad_pro` files are intentionally absent. Hardware design changes require the canonical editable KiCad project from its owner; do not reconstruct an authoritative design from the PDF or Gerbers.

