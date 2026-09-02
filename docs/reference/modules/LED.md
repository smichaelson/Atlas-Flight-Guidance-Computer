# RGB Status LED

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasLed_Init()` | Takes PB6/PB7/PD14 back from unused CubeMX TIM4 staging, configures push-pull GPIO outputs, and establishes a fail-dark state. |
| `AtlasLed_SetRgb()` | Sets the three logical GPIO channels and records the mask; physical green/blue mapping is unresolved. |
| `AtlasLed_SetColor()` | Validates and applies one of the eight binary RGB combinations. |
| `AtlasLed_Off()` | Removes all three channel drives and is safe to call repeatedly. |

Source: [`atlas_led.h`](../../../App/Inc/atlas_led.h) and [`atlas_led.c`](../../../App/Src/atlas_led.c).

## Validation state

The GPIO-ownership transition, fail-dark initialization, and logical color outputs pass deterministic host tests; the complete target image builds without warnings. Pin-to-die color, electrical polarity, current, brightness balance, color mixing, and reset appearance remain bench-pending.

**Mapping conflict:** the schematic connects `LED_G`/PB7 to D5's blue pin and `LED_B`/PD14 to its green pin. Firmware remains unchanged. See the [hardware mapping evidence](../HARDWARE.md#led-mapping-conflict) and R09 in the current review; do not treat enum names as verified physical colors.

## RTOS access

After scheduler start, set the LED only with an `ATLAS_RTOS_COMMAND_LED_SET` request. Queue acceptance and hardware execution are distinct; inspect the completion ticket/status. The direct examples below are appropriate before scheduling in a driver test, not in application/control code. Startup blue/green/yellow remains board-initialization behavior and is not a live task-health indicator.

## Board contract

| Logical channel | MCU pin | Schematic channel reached when high |
|---|---|---|
| Red | PB6 `LED_R` | D5 pin 2, red |
| Green | PB7 `LED_G` | D5 pin 4, blue |
| Blue | PD14 `LED_B` | D5 pin 3, green |

The LED package is common-anode, but the MCU drives transistor gates rather than LED cathodes directly. Accordingly, GPIO high means **on** and GPIO low means **off**. CubeMX stages these pins as TIM4 alternate functions even though Atlas does not start those PWM channels. `AtlasLed_Init()` first writes the output data registers low and then explicitly reconfigures PB6, PB7, and PD14 as push-pull GPIO outputs; that ownership transfer is required for `HAL_GPIO_WritePin()` to control the LED.

## Color contract

`AtlasLedColor` is a three-bit mask:

| Value | Logical channels (not verified emitted colors) |
|---|---|
| `ATLAS_LED_OFF` | none |
| `ATLAS_LED_RED` | red |
| `ATLAS_LED_GREEN` | green |
| `ATLAS_LED_BLUE` | blue |
| `ATLAS_LED_YELLOW` | red + green |
| `ATLAS_LED_MAGENTA` | red + blue |
| `ATLAS_LED_CYAN` | green + blue |
| `ATLAS_LED_WHITE` | red + green + blue |

Any value with bits outside `0x07` is rejected.

## Current board-level startup meaning

| Logical indication | Meaning |
|---|---|
| Off before project initialization | Fail-dark reset/default state |
| Blue | Bounded module bring-up in progress |
| Green | Every `AtlasBoardInitReport` field returned `ATLAS_OK` |
| Yellow | One or more startup checks failed |

These names describe commanded masks, not verified emitted colors. The GNSS shared-timer startup defect is corrected, but even a logical green result is not flight-ready: RFD transport startup does not prove modem/peer presence, and GNSS identity/configuration does not prove a navigation fix. Storage, USB and output services have separate runtime status; this startup mask does not certify them.

## Typical use

```c
(void)AtlasLed_SetColor(&atlas_board.led, ATLAS_LED_CYAN);
/* ... */
AtlasLed_Off(&atlas_board.led);
```

Future application meanings must be centralized in a status-indication service. Independent subsystems should not fight by writing colors directly.

## Failure behavior

- `AtlasLed_Init(NULL)` returns `ATLAS_ERROR_NULL`.
- Set operations before initialization return `ATLAS_ERROR_NOT_READY`.
- Invalid mask values return `ATLAS_ERROR_ARGUMENT` without applying the requested invalid state.
- `AtlasLed_Off()` ignores NULL/uninitialized objects.

The current HAL GPIO writes have no electrical feedback. A recorded color is a command state, not proof of emitted light.

## Bench acceptance

1. Observe reset/power sequencing and require all three channels off before intentional initialization.
2. Exercise all eight masks and map physical die color to PB6/PB7/PD14.
3. Verify GPIO-high turns each channel on and measure transistor gate/cathode/anode levels.
4. Measure channel current and component temperature against the schematic resistor network and LED ratings.
5. Check mixed-color recognizability in the installed enclosure and expected ambient light.
6. In an isolated test harness, inject pass/fail report outcomes and verify the intended logical masks against the measured mapping. Do not falsify production startup results to obtain a “green” board.
7. Trigger watchdog/brownout/debug reset and verify no unsafe or misleading persistent indication.

## Known limits

- No PWM dimming, blink scheduler, latching fault code, or LED electrical feedback exists.
- Binary full-intensity mixing may not look perceptually balanced.
- Direct color calls provide no ownership/priority arbitration.
- LED status must remain supplementary to retained diagnostics and telemetry.

## Primary references

- Board connectivity: [`hardware/Atlas-schematic-rev-0.1.pdf`](../../../hardware/Atlas-schematic-rev-0.1.pdf).
- MCU GPIO behavior: the STM32Cube-generated pin setup in [`Core/Src/main.c`](../../../Core/Src/main.c) and project-owned driver linked above.
