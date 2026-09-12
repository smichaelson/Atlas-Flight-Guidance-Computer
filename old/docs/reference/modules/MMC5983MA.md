# MMC5983MA Magnetometer

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasMmc5983ma_Init()` | Software-resets, proves the product ID, selects conversion bandwidth, and reads it back. |
| `AtlasMmc5983ma_UnpackRaw()` | Deterministically reconstructs three packed unsigned 18-bit values from the seven-byte register image. |
| `AtlasMmc5983ma_ReadField()` | Performs one bounded conversion with automatic SET/RESET and returns nominal gauss. |
| `AtlasMmc5983ma_ReadFieldSetReset()` | Takes explicit SET and RESET conversions and cancels bridge offset by half-difference. |
| `AtlasMmc5983ma_ReadTemperature()` | Triggers the internal temperature conversion and applies the documented nominal equation. |

Source: [`atlas_mmc5983ma.h`](../../../App/Inc/atlas_mmc5983ma.h) and [`atlas_mmc5983ma.c`](../../../App/Src/atlas_mmc5983ma.c).

## Validation state

Identity, reset behavior, bandwidth readback, packed 18-bit decoding, on-demand field conversion, and temperature conversion pass a deterministic register-emulator test; the complete target image builds without warnings. Physical field, SET/RESET, temperature, and magnetic calibration are bench-pending.

## RTOS access

`AtlasIO` runs the bounded automatic SET/RESET field conversion every 50 ms and publishes `AtlasRtosSnapshot.mmc5983ma`. Require `ATLAS_RTOS_VALID_MMC5983MA`, `mmc5983ma_status == ATLAS_OK`, and a reviewed age limit. Explicit paired SET/RESET offset cancellation and temperature reads are maintenance/test operations not currently exposed through the runtime command queue; add an I/O-owner command rather than calling the driver from application code.

## Board contract

| Item | Atlas value |
|---|---|
| Fitted part | MMC5983MA, U9 |
| Bus | Shared SPI2, 8-bit, MSB first, mode 3 |
| Bus clock | Approximately 3.125 MHz; below the documented 10 MHz limit |
| Chip select | `CS_MMC5983`, PG11, active low |
| Identity | Product ID `0x30` |
| Default bandwidth | 200 Hz selection / nominal 4 ms conversion latency |
| Operating style | On-demand; no continuous-mode stream is started |

## Startup transaction

1. Bind the shared SPI2 bus and force PG11 inactive.
2. Write software reset in Internal Control 1 and wait 10 ms.
3. Read Product ID and require `0x30`.
4. Write the requested bandwidth bits in Internal Control 1.
5. Read the register back and compare the selected bits.

## Data contract

The seven output bytes contain the upper 16 bits of X/Y/Z plus two low bits per axis in the shared XYZ-out-2 register. `AtlasMmc5983ma_UnpackRaw()` reconstructs values from 0 through 262143. For a normal single conversion:

```text
field_gauss = (raw - 131072) / 16384
```

For explicit bridge compensation:

```text
field_gauss = (set_raw - reset_raw) / (2 * 16384)
```

The second path is preferred for careful characterization because it rejects the bridge's null-field offset. Neither path performs hard-iron, soft-iron, alignment, or temperature calibration.

## Typical use

```c
AtlasMmc5983maField field;

if (AtlasMmc5983ma_ReadFieldSetReset(&atlas_board.mmc5983ma,
                                     &field,
                                     20U) == ATLAS_OK)
{
    /* Calibrate and rotate field.x_gauss/y_gauss/z_gauss before navigation use. */
}
```

The timeout is independently applied to each conversion in a paired reading; it is not a total operation-time bound. Keep the complete measurement under the existing `AtlasIO` ownership. A bus mutex around separate SPI transfers would not make driver state or SET/RESET sequences safe for concurrent use.

The 1 ms waits around explicit SET/RESET operations use the corrected `AtlasTime_DelayMs()` minimum-duration contract. Separate timing probes cover rounding, tick phase and early wakeups ([R02](../../REVIEW_REPORT.md#r02-rtos-waits-do-not-guarantee-minimum-device-delays)); still verify the physical command spacing on the fitted board.

## Health and fault interpretation

| Counter | Meaning |
|---|---|
| `io_errors` | SPI transaction failed. |
| `identity_failures` | Product ID was not `0x30`. |
| `configuration_mismatches` | Bandwidth readback disagreed. |
| `measurement_timeouts` | A magnetic or temperature done bit did not arrive in time. |
| `measurements` | Completed magnetic result structures. |

## Bench acceptance

1. Verify SPI mode 3 and that PG11/PG12 never assert together.
2. Capture reset, `0x30` identity, and bandwidth readback.
3. Test known synthetic register images against `AtlasMmc5983ma_UnpackRaw()` and retain the host-test result.
4. Collect normal and paired SET/RESET values in at least six orientations away from current-carrying wires.
5. Fit hard-iron offset, soft-iron matrix, scale, and the sensor-to-body transform from an adequate 3-D calibration set.
6. Repeat near expected vehicle current and RF states; document interference and saturation margins.
7. Force a stuck-not-ready condition and prove timeout/counter behavior.

## Known limits

- No continuous mode, interrupt, or automatic periodic SET configuration is enabled.
- Internal temperature is for device compensation/diagnostics and is not ambient truth.
- Nearby high-current event, radio, and power paths can dominate Earth's magnetic field.
- A magnetometer must not become a trusted navigation input until interference and calibration are characterized in the integrated vehicle.

## Primary reference

- MEMSIC, [MMC5983MA data sheet, Rev. A](https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5983MADatasheetRevA.pdf).
