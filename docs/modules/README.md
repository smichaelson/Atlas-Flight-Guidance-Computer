# Module Firmware Index

This directory is the entry point for Atlas hardware-facing firmware. Every fitted sensor, communications module, and user-feedback output has a dedicated implementation and an operational document. The drivers compile and link for STM32H743 and their deterministic math/parsers are host-tested; they still require the revision-matched bench acceptance tests described in each document before flight use.

## At a glance

| System | Board connection | Firmware | Default after `AtlasBoard_Init()` | Guide |
|---|---|---|---|---|
| BNO085 | I2C1, `NRST`, active-low interrupt | `atlas_bno085.*` + official CEVA SH-2 | Identified; accel/gyro/rotation at 100 Hz, magnetometer at 50 Hz | [BNO085](BNO085.md) |
| LSM6DSV16B | SPI3 mode 3, INT1 | `atlas_lsm6dsv16b.*` | 240 Hz, +/-16 g, +/-2000 dps, DRDY on INT1 | [LSM6DSV16B](LSM6DSV16B.md) |
| ADXL375 | SPI2 mode 3, dedicated CS | `atlas_adxl375.*` | Measurement mode, 400 Hz, fixed +/-200 g format | [ADXL375](ADXL375.md) |
| MMC5983MA | SPI2 mode 3, dedicated CS | `atlas_mmc5983ma.*` | Identified, 200 Hz bandwidth selection, on-demand conversion | [MMC5983MA](MMC5983MA.md) |
| MS5611-01BA03 | I2C1, address `0x77` | `atlas_ms5611.*` | Reset, PROM read, CRC-4 verified, on-demand conversion | [MS5611](MS5611.md) |
| NEO-M9N GNSS | USART1, TIM2 CH1 PPS | `atlas_gnss.*` | Identified, volatile 10 Hz NAV-PVT, UBX output, NMEA off, exact RAM readback | [GNSS](GNSS.md) |
| NINA-B112 BLE | USART6 RTS/CTS, reset/switches/DTR/DSR | `atlas_ble.*` | Normal boot and read-only identity probe; no automatic NVM write | [BLE](BLE.md) |
| External RFD900x | J9 / USART3 | `atlas_rfd900x.*` | Transparent byte transport only; no automatic `+++` or AT command | [RFD900x](RFD900X.md) |
| Differential buzzer | TIM15 CH1/CH2 | `atlas_buzzer.*` | Opposite-phase channels configured, output stopped | [Buzzer](BUZZER.md) |
| RGB status LED | Three active-high transistor gates | `atlas_led.*` | Green if all attempted startup transactions pass; yellow otherwise | [LED](LED.md) |

After `AtlasRtos_Start()`, those driver APIs are owned exclusively by `AtlasIO`. Application code uses `AtlasRtos_GetSnapshot()` for every sensor/GNSS value, `AtlasRtos_SubmitCommand()` for LED/buzzer/radio/BLE work, and `AtlasRtos_ReadRadio()` / `AtlasRtos_ReadBle()` for transparent receive data. A direct driver example in a module guide describes isolated pre-scheduler/driver testing only unless the example explicitly uses the RTOS API.

## Common interfaces

- [`atlas_board.h`](../../App/Inc/atlas_board.h) owns one instance of every driver, retains every startup result, routes interrupts, and services bounded foreground work.
- [`atlas_status.h`](../../App/Inc/atlas_status.h) defines stable status values shared by all project-owned modules.
- [`atlas_spi_device.h`](../../App/Inc/atlas_spi_device.h) provides explicit, timeout-bounded active-low chip-select transactions.
- [`atlas_uart_transport.h`](../../App/Inc/atlas_uart_transport.h) provides allocation-free receive-to-idle buffering for GNSS, BLE, and the radio.
- [`atlas_rtos.h`](../../App/Inc/atlas_rtos.h) defines the coherent snapshot, bounded command, receive-stream, application-hook, health, and supervision interfaces used after scheduler start.
- [`atlas_time.h`](../../App/Inc/atlas_time.h) yields a running task during driver waits while retaining HAL timing before scheduling.
- [Firmware architecture](../FIRMWARE_ARCHITECTURE.md) explains ownership, callback context, startup behavior, and data flow.
- [RTOS architecture](../RTOS_ARCHITECTURE.md) is the mandatory concurrency, priority, timing, watchdog, and application integration contract.
- [Bring-up and acceptance](../BRINGUP.md) gives the safe order and evidence template for physical validation.

## Validation language

The repository uses these terms deliberately:

| Term | Meaning |
|---|---|
| Implemented | Source and build-system membership exist. |
| Protocol-tested | Deterministic host tests cover framing, checksum, packing, or math where practical. |
| Target-built | The complete STM32 image compiled and linked with the Arm target compiler. |
| Bench-verified | A named board revision passed the documented test with retained evidence. |
| Flight-qualified | Requirements, environmental limits, calibration, fault behavior, and traceable verification have been formally accepted. |

None of the new module drivers is described as bench-verified or flight-qualified until physical evidence is added to the repository or linked release record.

An external RFD900x cannot be detected through the J9 header during nonintrusive startup. Consequently, a green LED does not prove radio presence, baud compatibility, pairing, RF transmission, or a GNSS/BLE connection.
