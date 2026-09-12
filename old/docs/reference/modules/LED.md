# RGB Status LED — Hardware Inhibited

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Current contract |
|---|---|
| `AtlasLed_Init()` | Writes PB6/PB7/PD14 low before reclaiming them from unused TIM4 staging, configures them as push-pull GPIO outputs, verifies low readback, and marks the RGB interface inhibited. |
| `AtlasLed_SetRgb()` | First writes every channel low. It returns `ATLAS_ERROR_UNSUPPORTED` if any requested channel is on and never writes a high level. |
| `AtlasLed_SetColor()` | Rejects bits outside `0x07`; valid nonzero masks reach the same mandatory inhibit. Only `ATLAS_LED_OFF` can succeed. |
| `AtlasLed_ReadGateMask()` | Samples the three legacy-named channel-control nets. On the affected board these are **not** correctly connected MOSFET gates. |
| `AtlasLed_Off()` | Unconditionally writes all three nets low, even when initialization did not complete. |

Source: [`atlas_led.h`](../../../App/Inc/atlas_led.h) and [`atlas_led.c`](../../../App/Src/atlas_led.c).

## Current disposition

**Do not attempt to illuminate D5 on Atlas rev-0.1.** The board owner confirmed the defect against the raw KiCad design after physical tests showed no light and no package-pin response. Q6, Q7, and Q8 use DMN3404L-7 devices, whose SOT-23 terminals are pin 1 gate, pin 2 source, and pin 3 drain. The affected PCB routing instead assigns the intended LED load to pad 1, MCU control through 220 Ω to pad 2, and ground to pad 3. Consequently the copper does not implement the intended low-side switch.

This is not a color-order or active-level firmware problem. Driving the legacy `LED_R`, `LED_G`, or `LED_B` net high cannot make the as-built stage a valid MOSFET switch. Earlier package-pad voltage observations are consistent with an invalid/floating network and must not be used to justify further drive tests.

Version `1.0.2` therefore establishes a defense-in-depth software boundary:

- `ATLAS_LED_HARDWARE_INHIBITED` is compile-time fixed to `1U`; changing it causes a build error in the driver.
- Board startup no longer requests blue, green, or yellow.
- All driver paths write PB6, PB7, and PD14 low before evaluating a request.
- Every nonzero direct or RTOS request returns `ATLAS_ERROR_UNSUPPORTED`.
- The bring-up wire protocol accepts only `led 0`, and the dashboard exposes no illumination control.
- The `hello` handshake requires `led_inhibited:true`, while status requires `led.inhibited=1` and `led.commanded=0`.

These controls reduce the chance of accidental energization. They do not repair the PCB, disconnect the network physically, or qualify the affected parts after prior testing.

## RTOS access

The only accepted RTOS LED command is `ATLAS_RTOS_COMMAND_LED_SET` with `ATLAS_LED_OFF`. Command validation rejects a valid nonzero color as `ATLAS_ERROR_UNSUPPORTED`; the driver repeats the low writes if a caller bypasses validation. Application/control code must use telemetry, USB, or the buzzer for status until a qualified hardware revision exists.

## Affected board contract

| Legacy channel name | MCU pin | Intended switch | Confirmed defect |
|---|---|---|---|
| `LED_R` | PB6 | R106 → Q8 → R105/D5 red path | MCU control reaches transistor pad 2/physical source; intended load reaches pad 1/physical gate; pad 3/physical drain is grounded |
| `LED_G` | PB7 | R102 → Q7 → R101/D5 green path | Same Q7 terminal mismatch |
| `LED_B` | PD14 | R104 → Q6 → R103/D5 blue path | Same Q6 terminal mismatch |
| Common-anode supply | `5V_SYS` | D5 supply path | Supply presence does not correct the three invalid low-side stages |

The names `gates` and `AtlasLed_ReadGateMask()` remain in the protocol/API for compatibility, but on rev-0.1 they mean the MCU-side channel-control pins only. A zero readback proves only that PB6/PB7/PD14 are being held low.

## Safe use

```c
AtlasStatus status = AtlasLed_SetColor(&atlas_board.led, ATLAS_LED_OFF);
/* Any nonzero AtlasLedColor is deliberately unsupported on rev-0.1. */
```

`AtlasLed_Off()` is suitable for unconditional shutdown/fault cleanup. Do not add direct `HAL_GPIO_WritePin(..., GPIO_PIN_SET)` calls, start TIM4 channels 1/2/3 for these nets, or weaken the dashboard handshake.

## Failure behavior

- `AtlasLed_Init(NULL)` returns `ATLAS_ERROR_NULL` after no object can be updated.
- Initialization returns `ATLAS_ERROR_IO` if any channel-control pin reads high after low-output setup and leaves `initialized=false` while retaining `output_inhibited=true`.
- Calls before successful initialization still force all three nets low, then return `ATLAS_ERROR_NOT_READY`.
- A valid nonzero mask forces all three nets low, records `ATLAS_LED_OFF`, and returns `ATLAS_ERROR_UNSUPPORTED`.
- An off request returns `ATLAS_ERROR_IO` if low pin readback cannot be confirmed.
- `AtlasLed_Off()` always performs the low writes; a NULL pointer only prevents state recording.

## Acceptance before any future re-enable

1. Keep D5 testing disabled on the current board. Verify PB6, PB7, and PD14 remain low through reset, startup, module probes, dashboard commands, RTOS startup, and watchdog/reset conditions.
2. Require the dashboard handshake to show `led_inhibited:true`; require every status frame to show `led.inhibited=1`, `led.commanded=0`, and normally `led.gates=0`.
3. If a control net reads high, power down and inspect MCU pin mode, shorts, leakage, and the affected network. Do not turn another channel on as a diagnostic.
4. Document an approved PCB rework or new board revision that maps DMN3404L terminals 1/2/3 to gate/source/drain correctly and independently reviews LED/resistor polarity and ratings.
5. On unpowered hardware, verify the revised pad-to-net mapping and absence of shorts. Then use a current-limited supply and qualify one channel at a time with gate, drain, current, light, and temperature measurements.
6. Only after hardware acceptance, update the schematic/board revision identifier, replace the hard compile-time boundary in a separately reviewed change, restore operator controls deliberately, and rerun host, target, startup, reset, and fault tests.

## Known limits

- Firmware cannot correct the rev-0.1 Q6-Q8 terminal mapping.
- Software low is not galvanic isolation and cannot prove component health.
- `AtlasLed_ReadGateMask()` has no drain, current, optical, or `5V_SYS` feedback.
- There is intentionally no startup-color or runtime visual-status function in version `1.0.2`.

## Primary references

- Board connectivity: [`hardware/Atlas-schematic-rev-0.1.pdf`](../../../hardware/Atlas-schematic-rev-0.1.pdf).
- Transistor terminal assignment: Diodes Incorporated, [DMN3404L datasheet](https://www.diodes.com/datasheet/download/DMN3404L.pdf).
- MCU GPIO setup: [`Core/Src/main.c`](../../../Core/Src/main.c).
- The raw KiCad defect confirmation is owner-supplied bench/design evidence and is intentionally not copied into this repository.
