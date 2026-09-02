# LSM6DSV16B Direct IMU

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasLsm6dsv16b_DefaultConfig()` | Returns the reviewed Atlas baseline: 240 Hz, +/-16 g, +/-2000 dps, accel/gyro data-ready on INT1. |
| `AtlasLsm6dsv16b_Init()` | Proves identity, performs bounded reset, and reads back every selected direct-data-path setting. |
| `AtlasLsm6dsv16b_DataReady()` | Requires both new accelerometer and gyroscope data flags. |
| `AtlasLsm6dsv16b_ReadSample()` | Burst-reads temperature, gyro, and the device-specific acceleration register order, then applies nominal scaling. |
| `AtlasLsm6dsv16b_OnInterrupt()` / `ConsumeInterrupt()` | Records/coalesces INT1 edges without touching SPI in interrupt context. |

Source: [`atlas_lsm6dsv16b.h`](../../App/Inc/atlas_lsm6dsv16b.h) and [`atlas_lsm6dsv16b.c`](../../App/Src/atlas_lsm6dsv16b.c).

## Validation state

Identity, reset completion, exact control/interrupt values, ready flags, temperature/gyro data, and the documented Z/Y/X accelerometer register order pass a deterministic register-emulator test; the complete target image builds without warnings. Physical SPI, interrupt polarity/rate, axes, and sensor output remain bench-pending.

## RTOS access

The EXTI callback only increments the coalesced INT1 counter. `AtlasIO` reacts to that counter and also polls every 5 ms as a missed-edge fallback, then publishes only when both accelerometer and gyroscope data are ready. Read `AtlasRtosSnapshot.lsm6dsv16b` only with `ATLAS_RTOS_VALID_LSM6DSV16B`, an `ATLAS_OK` status, and a checked sample age. The direct example below is pre-scheduler test code; it is not a supported call from `AtlasRtos_ApplicationStep()`.

## Board contract

| Item | Atlas value |
|---|---|
| Fitted part | LSM6DSV16BTR, U27 |
| Bus | SPI3, 8-bit, MSB first, mode 3 |
| Bus clock | Approximately 3.125 MHz; below the 10 MHz driver ceiling |
| Chip select | `CS_LSM6DSV16B`, PG10, active low |
| INT1 | PG2 / EXTI2; ISR records an edge only |
| INT2 | Test point TP11; not used by firmware |
| Identity | `WHO_AM_I = 0x71` |
| Default | 240 Hz accel and gyro, +/-16 g, +/-2000 dps |

## Startup transaction

1. Read and require `WHO_AM_I = 0x71`.
2. Set `CTRL3.SW_RESET` and poll for self-clear for no more than 50 ms.
3. Set and verify block-data-update and register auto-increment.
4. Write/read back accelerometer full scale in `CTRL8`.
5. Write/read back gyroscope full scale in `CTRL6`.
6. Write/read back accelerometer and gyroscope ODRs in `CTRL1`/`CTRL2`.
7. Route both data-ready sources to INT1 when requested and verify `INT1_CTRL`.

The driver does not enable FIFO, embedded sensor fusion, finite-state machines, or the separate audio path.

## Data contract

The 14-byte read beginning at `OUT_TEMP_L` is decoded as signed little-endian words. The fitted part's acceleration register map is handled explicitly rather than assuming the order used by another LSM6 family member.

At the default ranges:

```text
acceleration_g = raw * 0.000488 g/LSB
angular_rate_dps = raw * 0.070 dps/LSB
temperature_c = 25 + raw_temperature / 256
```

The result is in the sensor-native frame, and `timestamp_ms` is the host time after the SPI burst. Future control code must add a verified board/body transform and measurement-age limit.

## Typical use

```c
if (AtlasLsm6dsv16b_ConsumeInterrupt(&atlas_board.lsm6dsv16b))
{
    AtlasLsm6dsv16bSample sample;
    if (AtlasLsm6dsv16b_ReadSample(&atlas_board.lsm6dsv16b, &sample) == ATLAS_OK)
    {
        /* Apply calibration and the reviewed sensor-to-body transform here. */
    }
}
```

Multiple edges may coalesce into one foreground indication. Use the health interrupt count and sequence timing to detect an acquisition task that cannot keep up.

## Health and fault interpretation

| Counter | Meaning |
|---|---|
| `io_errors` | SPI transaction failed. |
| `identity_failures` | `WHO_AM_I` did not equal `0x71`. |
| `configuration_mismatches` | A selected control bit did not read back. |
| `reset_timeouts` | `SW_RESET` did not clear within 50 ms. |
| `interrupt_count` | INT1 edges routed by the HAL callback. |
| `samples_read` | Successful complete bursts. |

`ATLAS_ERROR_NOT_READY` means both fresh-data flags were not simultaneously set; it is not a zero-valued measurement.

## Bench acceptance

1. Verify SPI mode 3, PG10 CS behavior, `0x71`, reset self-clear, and every control readback.
2. Scope PG2 and confirm its electrical polarity and 240 Hz behavior under the chosen routing.
3. Capture at least 1,000 interrupts and reads; compare counts and demonstrate the acquisition loop does not silently lag.
4. Establish each axis sign with static gravity and a slow, independently referenced rotation.
5. Check stationary bias/noise and temperature movement without treating a room-temperature check as calibration.
6. Force wrong identity, missing INT1, and SPI timeout cases; confirm bounded failure and no stale sample acceptance.

## Known limits

- No FIFO/watermark handling, self-test, filtering selection, timestamp register, or sensor-hub feature is configured.
- INT1 behavior and board-level pull configuration require physical confirmation.
- Nominal sensitivities are not calibrated scale factors.
- The direct IMU and BNO085 clocks are not synchronized.

## Primary reference

- STMicroelectronics, [LSM6DSV16B data sheet, DS14172 Rev. 2](https://www.st.com/resource/en/datasheet/lsm6dsv16b.pdf).
