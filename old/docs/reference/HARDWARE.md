# Rev. 0.1 hardware reference

This guide summarizes the [ten-page schematic export](../../hardware/Atlas-schematic-rev-0.1.pdf) and the compiled [pin definitions](../../Core/Inc/main.h)/[MSP setup](../../Core/Src/stm32h7xx_hal_msp.c). It is not an as-built inspection, electrical certification or fabrication approval. Runtime capability and acceptance live in [Systems](../SYSTEMS.md).

## Authority and limits

The board uses STM32H743ZIT6 (2 MiB flash). The schematic title block is Rev. 0.1, dated 2026-03-19. Confirm the assembled revision and exact fitted parts before relying on a mapping. Device data-sheet axes are not automatically the board/body frame.

Current source assumes an 8 MHz HSE, 200 MHz CPU, 100 MHz HCLK, 50 MHz APB buses and 100 MHz timer kernels for the used timer configuration. PLL2 provides the 50 MHz SPI/USART/SDMMC/ADC kernel selections; PLL3 provides 16 MHz I2C and 48 MHz USB selections. These are source-derived nominal values, not measured clock accuracy.

Raw KiCad projects are intentionally absent. Obtain the canonical editable design from its owner for hardware changes. [Hardware package](../../hardware/README.md) and [candidate manufacturing manifest](../../hardware/manufacturing/README.md) retain their separate provenance.

## Power and physical inhibit

| Path | Intended role |
|---|---|
| J1 → `VIN_RAW` | Main power entry; polarity must be checked |
| `VIN_RAW` → U11 TPS16850 → `VIN_PROT` | Main protected input feeding regulators |
| `VIN_PROT` → MP8772 → `8V4_PWM` | Shared nominal 8.4 V actuator-header power |
| `VIN_PROT` → LM76003 → `5V_SYS` | Radio/header and other 5 V loads |
| `VIN_PROT` → LM76002 → `3V3_SYS` | MCU digital and 3.3 V loads |
| Ferrite-filtered `3V3_AUX` | Analog, GNSS/BLE and low-noise branch |
| **`VIN_RAW` → physical J5 arm link → `PYRO_VBAT_ARMED`** | **Bypasses U11; the main eFuse does not protect this pyro feed** |

The prior hardware overview's “shared input protection” claim for pyro current was incorrect and is withdrawn. No independent per-channel fuse/current limiter is shown in those paths. Keep J5 open and energetic loads disconnected during ordinary work.

The schematic states a 16.3 V maximum input and an undercharged four-cell source. Treat that as the present boundary until the owner supplies an approved input specification; a fully charged nominal 4S pack can exceed it. An overvoltage fault cutoff is not an operating allowance. No separate reverse-polarity stage is shown ahead of `VIN_RAW`; use current-limited bench power and confirm polarity.

J9 is directly on `5V_SYS`, not behind an accessory TPS2553 limiter. Budget radio transmit peaks and power integrity using the exact modem specification. Two 5 V and two 3.3 V accessory ports have TPS2553 limiters, but their enables are tied high and fault outputs are not monitored by this firmware. They are not software power cutoffs.

PWM headers share a live power rail with no individual electronic limiter per channel. Stopped PWM does not mean an unpowered actuator connector. USB VBUS is sensed, not used to supply the system rails.

The populated R32=133 kΩ / R33=13.7 kΩ divider with a typical 1.21 V TPS1685x UVLO threshold implies approximately 12.96 V rising input enable. A 12 V supply may therefore not start the board. Verify as-built values and limits; the staged 15 V current-limited procedure and meter points are in [startup](../startup.md#3-first-power--no-usb-no-card-no-external-loads), not an approved battery/load envelope.

## Boot and USB as-built gates

STM32 BOOT0 is TP8/U6 pin 138 (10 kΩ pull-down); NRST is TP13/U6 pin 25 (10 kΩ pull-up and capacitor). TP12 is **GNSS** SAFEBOOT, not STM32 BOOT0. J28 is a custom connector, not a standard Cortex-debug pinout. The complete [programming/pin procedure](../startup.md#2-unpowered-inspection-and-pin-map) includes the required ground/reference connections and power sequence.

**Newly identified discrepancy:** PDF page 5 connects J2's drawn D− side to the net named USB_D+ and D+ side to USB_D−. Manufacturing copper pad attributes instead identify J2 A6/B6 → U16 I/O1 → PA12 D+, and A7/B7 → U16 I/O2 → PA11 D−, the expected polarity. Attributes are not a continuity test or a certification of actual copper/assembly. Require the [unpowered USB continuity check](../startup.md#usb-data-polarity-is-a-required-as-built-check) before USB bring-up. Resolve any reversal in hardware; the USB PHY cannot swap pins in firmware. U16's unconnected-looking VBUS support pin also needs protection-circuit review. No hardware exports were changed to conceal either discrepancy.

## Sensor and communications connections

Addresses below are seven-bit I2C addresses; the HAL adapter applies the required address shift.

| Device/interface | MCU connection | Source-defined baseline |
|---|---|---|
| SPI2 bus | PB10 SCK, PC2_C MISO, PC3_C MOSI | Mode 3, prescaler 16, nominal 3.125 MHz; inspect actual signal integrity |
| ADXL375 U1 | SPI2; CS PG12; board MOSI gating | 400 Hz, 4-wire/right-justified |
| MMC5983MA U9 | SPI2; CS PG11 | On-demand conversion, 200 Hz bandwidth code |
| SPI3 bus / LSM6DSV16B U27 | PB3 SCK, PB4 MISO, PB2 MOSI; IMU CS PG10, INT1 PG2 | Mode 3, nominal 3.125 MHz; INT2 at TP11 |
| I2C1 bus | PB8 SCL, PB9 SDA | Shared BNO085/MS5611; external pull-ups |
| BNO085 U12 | I2C1 `0x4A`; reset PB13, active-low interrupt PG0 | SA0/H_MOSI is strapped low; four SH-2 reports; shares TIM2 timebase with GNSS |
| MS5611 U14 | I2C1 `0x77` | PROM/CRC, compensated D2/D1 conversion |
| NEO-M9N U23 | USART1 MCU TX PB14 / RX PB15; PPS PA15 TIM2 CH1 | 38400 8N1 host; stale-RX preflight before `MON-VER`; intended RAM-only 10 Hz NAV-PVT; external antenna J27; reset not MCU-routed |
| NINA-B112 U21 | USART6 MCU TX PG14 / RX PG9, RTS PG8 / CTS PG15 | 115200 8N1 RTS/CTS; reset PD4, SWITCH1 PG5, SWITCH2 PG4, module DSR driven PG6, DTR observed PE1 |
| RFD900x J9 | USART3 MCU TX PD8 / RX PB11; 5 V and ground | 115200 8N1 host; no routed radio hardware flow control |
| microSD | SDMMC1 D0–D3 PC8–PC11, CK PC12, CMD PD2; detect PD3 | One-bit initialization then four-bit / 25 MHz polling; PD3 active-low mechanical J3 detect |
| USB full-speed device | PA11 D−, PA12 D+; divider-sensed VBUS PA9 | VBUS-gated CDC owner; PIO, no USB DMA |
| External UART | UART4 MCU RX PD0 / TX PD1 | 115200 8N1 initially; explicit raw expansion service |
| External I2C | I2C2 SDA PF0 / SCL PF1 | Raw/register transaction service; attached-device contract required |
| External SPI | Shared SPI3; external CS **PG13** | Raw transaction service; mode 3 / nominal 3.125 MHz, serialized with the IMU |

J9's **RFD900x** assignment was explicitly confirmed by the owner on 2026-09-01. The schematic and generated `LORA_*` labels are legacy names. RFD900x uses its serial-modem/SiK interface, not a Semtech LoRa register protocol.

## Outputs and monitoring

| Group | Mapping / limitation |
|---|---|
| PWM1–4 | TIM1 CH1–4: PE9, PE11, PE13, PE14 |
| PWM5–8 | TIM3 CH1–4: PC6, PC7, PB0, PB1 |
| PWM setup | Runtime prescaler 99, period 3002: 1 MHz / 333.0003 Hz; disabled/GPIO-low until qualified enable |
| GPIO outputs 1–7 | PF2, PE4, PA5, PA7, PC5, PF14, PF13; initialized low |
| GPIO inputs 1–7 | PE2, PE3, PG1, PE7, PE8, PE10, PE15; raw input snapshot; application polarity/debounce/semantics remain explicit |
| External switch | PF12 raw input snapshot; no automatic arming/flight meaning |
| Pyro gates 1–5 | PD9–PD13; disarmed/low; qualified TIM6 + DMA1 Stream1 two-edge pulse service |
| Buzzer | PE5 TIM15 CH1 / PE6 CH2; runtime PWM1/PWM2 opposite phase; stopped initially |

TIM15's nominal 4.8 kHz request rounds to 208 counter counts (about 4807.7 Hz). Measure both terminals and their differential voltage. A scheduled beep stops only when I/O service reaches its expiry check.

| Analog source | ADC1 sequence / pin | Nominal conversion aid—not a threshold |
|---|---|---|
| `3V3_SYS` | Rank 1 / IN16 / PA0 | Divider ×2 |
| `8V4_PWM` | Rank 2 / IN10 / PC0 | Divider approximately ×3.105 |
| `5V_SYS` | Rank 3 / IN11 / PC1 | Divider ×2 |
| `VIN_PROT` | Rank 4 / IN2 / PF11 | Divider approximately ×9.133 |
| Armed bus | Rank 5 / IN17 / PA1 | 820 kΩ / 100 kΩ divider |
| Continuity 1–5 | Ranks 6–10 / PA2, PA3, PA4, PA6, PC4 | 470 kΩ feed, 100 kΩ return, 10 nF filtering |

The output owner calibrates both ADCs. ADC1 uses a completed-only ten-rank scan in AXI SRAM; ADC3 uses separate single-rank VREFINT and temperature conversions. Runtime overrides the generator's original ADC3 sequencing. The snapshot and bring-up status retain the first ADC3 operation/channel/raw/HAL failure independently from the external ADC1/DMA error count. Those diagnostics localize a software/hardware boundary but do not qualify reference accuracy. Divider ratios, reference/offset, settling, tolerances and safe diagnostic current require assembled-board validation. Do not derive firing permission from nominal divider arithmetic.

J5 has no independent digital contact sensor. The qualified armed-feed voltage is a supply-present proxy, not proof that a safe arm link/load is fitted. Continuity is UNKNOWN without that supply and during firing. The ADC equivalent drain multiplier is 5.7; the armed-supply multiplier is 9.2. Thresholds are application-supplied only after measurement; no qualified defaults exist.

## RGB LED hardware defect and mandatory inhibit

The board owner confirmed the D5 orientation, then identified the controlling defect in the raw KiCad design on 2026-09-04. The fitted Q6/Q7/Q8 type is DMN3404L-7, whose SOT-23 terminals are pin 1 gate, pin 2 source, and pin 3 drain. The PCB routing instead sends the intended LED load to transistor pad 1, MCU control through 220 Ω to pad 2, and ground to pad 3. Thus all three intended low-side stages have gate/source/drain assigned to the wrong physical terminals. D5 orientation or a software color swap cannot correct this.

| Legacy control / intended channel | Affected transistor path | Rev-0.1 result |
|---|---|---|
| PB6 `LED_R` / red | R106 → Q8 → R105/D5 | MCU drives pad 2/physical source; load reaches pad 1/physical gate; pad 3/physical drain is grounded |
| PB7 `LED_G` / green | R102 → Q7 → R101/D5 | Same terminal mismatch |
| PD14 `LED_B` / blue | R104 → Q6 → R103/D5 | Same terminal mismatch |
| `5V_SYS` / common anode | D5 supply | A correct anode supply does not repair the invalid switches |

Version 1.0.2 permanently treats PB6/PB7/PD14 as fail-dark outputs on this revision: initialization writes and verifies low, board startup has no color indications, nonzero requests are unsupported, and the updated dashboard requires explicit LED-inhibit telemetry. Do not drive these nets high or perform further illumination tests until approved hardware rework/a corrected PCB revision is electrically inspected and qualified. See the dedicated [RGB LED guide](modules/LED.md).

## Verification boundary

All schematic pages were visually checked for this review, including USB and the output/power paths. That did not validate PCB routing, manufacturing alignment, component substitutions, assembled wiring or electrical margins. [Systems acceptance gates](../SYSTEMS.md#acceptance-gates) and the [review report](../REVIEW_REPORT.md) retain those limitations.
