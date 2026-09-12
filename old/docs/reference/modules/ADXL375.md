# ADXL375 High-g Accelerometer

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasAdxl375_Init()` | Proves identity, enters standby, writes and reads back the data format/ODR/interrupt policy, then enters measurement mode. |
| `AtlasAdxl375_DataReady()` | Polls the latched data-ready source because this board does not route either ADXL375 interrupt output. |
| `AtlasAdxl375_ReadSample()` | Reads XYZ in one six-byte burst and returns both signed counts and nominal g. |
| `AtlasAdxl375_SetMeasurementEnabled()` | Performs an explicit, read-back-verified standby/measurement transition. |

Source: [`atlas_adxl375.h`](../../../App/Inc/atlas_adxl375.h) and [`atlas_adxl375.c`](../../../App/Src/atlas_adxl375.c).

## Validation state

Identity, exact startup register values, configuration readback, data-ready status, six-byte burst order, signed conversion, and nominal scaling pass a deterministic register-emulator test; the complete target image builds without warnings. Physical SPI traffic and measurements are not yet bench-verified on an Atlas Rev. 0.1 board. Do not use the nominal scaling or axes for control until the acceptance procedure below passes.

## RTOS access

`AtlasIO` requests a DATA_READY poll when the 2 ms period is due and publishes successful samples in `AtlasRtosSnapshot.adxl375`. Blocking work in the same task can delay that poll; it does not guarantee delivery of every 400 Hz device sample. Require `ATLAS_RTOS_VALID_ADXL375`, check `adxl375_status`, and apply an application-approved age limit to `timestamp_ms`. After scheduler start, application code must not call the direct API or touch the shared SPI2 bus; the direct example below is for an isolated pre-scheduler driver test.

## Board contract

| Item | Atlas value |
|---|---|
| Fitted part | ADXL375BCCZ, U1 |
| Bus | SPI2, 8-bit, MSB first, mode 3 |
| Bus clock | Approximately 3.125 MHz from the current 50 MHz kernel / 16; below the driver's 5 MHz ceiling |
| Chip select | `CS_ADXL375`, PG12, active low |
| Interrupts | Not routed; polling only |
| Default ODR | 400 Hz (`BW_RATE = 0x0C`) |
| Output format | Fixed +/-200 g, right-justified, signed little-endian words; nominal 49 mg/LSB |
| Transaction timeout | 20 ms per SPI transaction |

The board contains ADXL-specific serial-line gating. That circuitry and the exact assembled population must be checked on a logic analyzer; the driver does not attempt to repurpose the shared SPI2 bus while its chip select is inactive.

## Startup transaction

1. Bind SPI2 and force PG12 inactive.
2. Read `DEVID` (`0x00`) and require `0xE5`.
3. Clear `POWER_CTL.Measure` and verify standby.
4. Write `DATA_FORMAT = 0x0B`, selecting four-wire SPI and the part's required fixed bits; verify all bits.
5. Write the requested `BW_RATE`; verify the rate/power bits.
6. Write `INT_ENABLE = 0x00`; verify that no unrouted interrupt is enabled.
7. Set and verify `POWER_CTL.Measure`.

Any failed identity or readback leaves `initialized == false` and increments the corresponding health counter.

## Data contract

`AtlasAdxl375Sample` preserves the raw `int16_t` values and calculates:

```text
axis_g = raw_axis * 0.049 g/LSB
```

`timestamp_ms` is the MCU tick at the end of the burst, not the sensor's sampling instant. XYZ is in the sensor's native coordinate frame. A board-to-body transform, bias model, scale calibration, and saturation policy remain application responsibilities.

## Typical use

```c
bool ready;
AtlasAdxl375Sample sample;

if ((AtlasAdxl375_DataReady(&atlas_board.adxl375, &ready) == ATLAS_OK) && ready)
{
    if (AtlasAdxl375_ReadSample(&atlas_board.adxl375, &sample) == ATLAS_OK)
    {
        /* Consume sample.x_g/y_g/z_g only after the board-frame transform exists. */
    }
}
```

## Health and fault interpretation

| Counter | Meaning |
|---|---|
| `io_errors` | HAL SPI busy, timeout, or error result. |
| `identity_failures` | `DEVID` was not `0xE5`; suspect bus mode, CS, power, wrong part, or assembly. |
| `configuration_mismatches` | A critical register did not read back as written. |
| `samples_read` | Successful coherent XYZ bursts. |

Never convert a failed read into a zero-g sample. The RTOS supervisor already enforces a 50 ms baseline age after applicable startup/recovery grace; application algorithms may require tighter age and quality limits.

## Bench acceptance

1. Confirm PG12 is high at reset and only low for ADXL transactions; the MMC5983MA CS must remain high.
2. Decode SPI mode 3 and verify the first identity response is `0xE5`.
3. Verify the exact startup sequence and readbacks above.
4. Capture at least 100 data-ready polls and bursts; verify device ODR separately from delivered sample rate and quantify losses/jitter under combined I/O load.
5. Rotate each board axis through +1 g and -1 g orientations and establish the native-to-board transform.
6. Apply only a qualified, bounded high-g stimulus; verify sign, saturation, bandwidth, fixture resonance, and sample loss.
7. Disconnect or hold the device unavailable and confirm every operation returns within its timeout without selecting another SPI device.

## Known limits

- No FIFO, activity/shock interrupt, offset register, or self-test workflow is configured.
- The 49 mg/LSB value is nominal and is not a calibration certificate.
- Host tick timestamps do not resolve the actual sample phase.
- This consumer MEMS path is not a substitute for a qualified flight measurement chain.

## Primary reference

- Analog Devices, [ADXL375 data sheet, Rev. B](https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL375.PDF).
