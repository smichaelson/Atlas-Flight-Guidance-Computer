# Review 1 — Hardware and Protocol Conformance

| Field | Value |
|---|---|
| Date | 2026-09-01 |
| Baseline | Atlas Flight Computer schematic Rev. 0.1, dated 2026-03-19 |
| Scope | Fitted sensors, GNSS, NINA-B112, external RFD900x interface, RGB LED, and differential buzzer |
| Method | Independent pass over schematic-derived nets, generated peripheral configuration, driver transactions, and primary manufacturer manuals |
| Outcome | Pass for implementation consistency; physical acceptance remains on hold |

## Questions applied

1. Does the code address the exact fitted part, bus instance, address/chip select, interrupt, reset, and output polarity shown by the hardware reference?
2. Are startup commands, register encodings, byte order, checksums, reset delays, mode transitions, and configuration readbacks consistent with the primary manufacturer interface documents?
3. Can an acknowledgement, echo, timeout, or stale software flag be mistaken for proof of active configuration?
4. Does ordinary boot avoid unrequested nonvolatile writes and unsafe output activity?

## Coverage and result

| System | Conformance focus | Result |
|---|---|---|
| ADXL375 | SPI2 mode 3, PG12 select, `0xE5` identity, fixed format, ODR, data-ready polling, XYZ little-endian burst | Source consistent; register emulator passed; bench pending |
| LSM6DSV16B | SPI3 mode 3, PG10 select, PG2 INT1, `0x71` identity, reset, ODR/FS, direct-output map | Source consistent; register emulator passed; bench pending |
| MMC5983MA | SPI2 mode 3, PG11 select, `0x30` identity, control-bit encodings, 18-bit packing, SET/RESET | Source consistent; register emulator passed; bench pending |
| MS5611-01BA03 | I2C1 address `0x77`, reset/PROM sequence, CRC-4, conversion commands, signed 64-bit compensation | Source consistent; command/math emulator passed; bench pending |
| BNO085 | I2C1 address `0x4B`, PB13 reset, PG0 interrupt, CEVA SHTP/SH-2 adapter, report IDs and reset handling | Source and pinned library consistent; physical SHTP trace pending |
| NEO-M9N | USART1, TIM2 PPS capture, UBX checksum, MON-VER, NAV-PVT offsets, RAM CFG keys and typed values | Source consistent; parser/config/PPS tests passed; bench pending |
| NINA-B112-05B | USART6 RTS/CTS, normal boot straps, reset/DSR/DTR polarity, R55 AT syntax, SPS role/server/start mode | Source consistent; AT/profile/mode tests passed; bench pending |
| RFD900x header | USART3 transparent transport, SiK escape guards, local AT access, readback before save | Source consistent; AT emulator passed; modem/region/paired link pending |
| RGB LED | PB6/PB7/PD14 low-side transistor gates and active-high logical colors | Runtime GPIO ownership and logic test passed; electrical color/polarity pending |
| Buzzer | PE5/PE6 TIM15 differential drive, opposite PWM modes, bounded frequency and silent stop | Timer contract test passed; waveform/load measurement pending |

## Findings resolved in this baseline

- **R1-01 — Radio terminology:** The J9 interface had been described informally as LoRa. Hardware and RFDesign documentation identify an external RFD900x/SiK frequency-hopping serial modem; code and documentation now use `RFD900x` and explicitly reject a LoRa/LoRaWAN claim.
- **R1-02 — GNSS configuration proof:** A CFG-VALSET acknowledgement alone was insufficient. Initialization now performs CFG-VALGET against RAM and requires all six typed keys to match, independent of response order.
- **R1-03 — BLE operating mode:** u-connectXpress R55 defines `UMSM=1` as ordinary Data mode and `UMSM=2` as Extended Data Mode. The saved SPS profile uses `UMSM=1`; wired `AT&D1` command entry requires the unsolicited `OK` before a fresh AT probe.
- **R1-04 — BLE persistence:** Persistent configuration now uses `AT&W`, restart, complete profile re-read, an operational DSR transition check, and verified `ATO1` return rather than treating write acknowledgements as durable proof.
- **R1-05 — LSM6DSV16B output order:** The direct accelerometer registers are Z, Y, X at `0x28`, `0x2A`, `0x2C`; the driver and test retain this non-obvious map explicitly.
- **R1-06 — LED pin ownership:** CubeMX stages the LED pins as TIM4 alternate functions. `AtlasLed_Init()` now establishes fail-dark output levels and deliberately reclaims all three pins as GPIO before color control.
- **R1-07 — BNO085 asynchronous faults:** CEVA transport and reset counters are surfaced through the adapter; newly observed protocol/I/O/decode/reset faults latch the board runtime fault instead of being silently ignored.

## Primary references

The module guides under [Module Firmware Guides](../modules/README.md) link the exact Analog Devices, ST, MEMSIC, TE Connectivity, CEVA, u-blox, RFDesign, and Murata primary documents used for this pass. No secondary tutorial was treated as protocol authority.

## Residual hold points

- No assembled Atlas board was available to confirm population, rework state, bus electrical behavior, interrupt polarity, reset timing, output polarity, or sensor orientation.
- No BNO085 SHTP trace, GNSS fix/PPS measurement, BLE RF/SPS session, or RFD900x paired-radio session has been captured.
- Nominal sensor scaling is not calibration, and no body-frame transform is approved.
- Passing this review therefore supports implementation and host-protocol status only. Follow [Module Bring-up and Acceptance](../BRINGUP.md) before promoting any system to bench-verified.
