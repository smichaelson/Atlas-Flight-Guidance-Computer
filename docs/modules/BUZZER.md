# Differential Piezo Buzzer

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasBuzzer_Init()` | Confirms TIM15, derives its kernel clock, programs opposite-phase channels at the resonant default, and leaves both outputs stopped. |
| `AtlasBuzzer_Start()` | Reprograms a bounded 1-10 kHz frequency and starts both differential PWM channels. |
| `AtlasBuzzer_Beep()` | Starts a nonblocking tone with a wrap-safe, bounded stop deadline. |
| `AtlasBuzzer_Service()` | Stops a scheduled beep when its deadline is reached. |
| `AtlasBuzzer_Stop()` | Stops both channels to eliminate differential drive. |

Source: [`atlas_buzzer.h`](../../App/Inc/atlas_buzzer.h) and [`atlas_buzzer.c`](../../App/Src/atlas_buzzer.c).

## Validation state

TIM15 ownership, opposite PWM modes, valid/invalid frequency handling, continuous stop, and wrap-safe timed stop pass deterministic host tests; the complete target image builds without warnings. Physical frequency, phase, stopped levels, voltage across the sounder, acoustic output, and EMC remain bench-pending.

## RTOS access

Queue `ATLAS_RTOS_COMMAND_BUZZER_BEEP` with a validated 1–10 kHz frequency and bounded nonzero duration, or queue `BUZZER_STOP`. `AtlasBoard_Service()` runs in `AtlasIO` and ends timed tones without application polling. The direct example below is limited to pre-scheduler driver testing; a runtime direct call would violate TIM15/driver ownership.

## Board contract

| Item | Atlas value |
|---|---|
| Fitted part | Murata PKMCS0909E48H0-R1, externally driven piezo sounder |
| MCU timer | TIM15 |
| Low-side waveform pin | PE5 / TIM15 channel 1 |
| High-side waveform pin | PE6 / TIM15 channel 2 |
| Drive method | Two equal-duty outputs, PWM1 and PWM2, logically opposite |
| Recommended default | 4.8 kHz square wave |
| Allowed API range | 1,000 through 10,000 Hz |
| Startup | Silent; timer channels are not started |

With the current 100 MHz TIM15 kernel and prescaler 99, the PWM counter is 1 MHz. The rounded 4.8 kHz setup uses 208 counts and therefore produces approximately 4,807.7 Hz. Runtime setup intentionally overrides the CubeMX-generated same-phase 4 kHz staging values; a future regeneration must preserve or deliberately incorporate the driver behavior.

## Differential waveform

Channel 1 uses PWM mode 1 and channel 2 PWM mode 2 with the same half-period compare. Therefore one buzzer terminal is high while the other is low, reversing the field each half-cycle. Two same-phase PWM channels would create nearly zero differential audio drive even though each pin looks active by itself.

Always measure **PE5 minus PE6** as well as each pin to ground. Stop must leave zero differential waveform.

## Typical use

```c
/* Nonblocking 120 ms acknowledgement tone near the part's resonance. */
(void)AtlasBuzzer_Beep(&atlas_board.buzzer,
                       ATLAS_BUZZER_RESONANT_FREQUENCY_HZ,
                       120U);

/* AtlasBoard_Service() calls AtlasBuzzer_Service() to end the tone. */
```

`duration_ms` must be nonzero and less than `2^31` ms so signed wrap-safe deadline comparison is valid. A continuous `AtlasBuzzer_Start()` must always have a higher-level stop/fault policy.

## Failure behavior

- Initialization rejects any timer other than TIM15 or a zero derived clock.
- An out-of-range/unrepresentable frequency returns `ATLAS_ERROR_ARGUMENT` without starting output.
- If channel 2 fails to start, channel 1 is stopped before returning an I/O error.
- `AtlasBuzzer_Stop()` is idempotent and safe on a partially initialized object.
- The watchdog does not depend on buzzer activity.

## Bench acceptance

1. Verify both pins are electrically benign through reset, initialization, watchdog reset, and stop.
2. At 4.8 kHz, scope PE5, PE6, and PE5-PE6; require approximately 50% duty and 180-degree logical phase opposition.
3. Measure actual frequency and compare with `buzzer.frequency_hz`.
4. Exercise 1 kHz, 4.8 kHz, and 10 kHz plus invalid boundary requests.
5. Verify a timed beep ends at the requested duration within foreground-service jitter and through HAL tick wrap simulation.
6. Confirm differential peak voltage remains within the exact sounder specification for board supply/tolerance and that neither MCU output exceeds current/electrical limits.
7. Characterize sound pressure, enclosure resonance, temperature, power noise, EMI, and audibility for the intended alerts.

## Known limits

- The buzzer is an indicator, not a safety annunciator with independent supervision.
- No melody/priority queue, volume control, fault feedback, or acoustic self-test exists.
- A blocked `AtlasIO` task can delay a scheduled stop until the watchdog resets the MCU; continuous tones require explicit control discipline.
- The Murata family is scheduled for discontinuation; any replacement needs electrical/acoustic requalification.

## Primary reference

- Murata, [sound-components catalog entry for PKMCS0909E48H0-R1](https://www.murata.com/~/media/webrenewal/support/library/catalog/products/k70e.ashx?la=en-us) (4.8 kHz external square-wave drive; consult the part specification for limits).
