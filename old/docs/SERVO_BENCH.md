# KST X10 V8.0 servo workbench

This is manual, inert bench testing with the separate **Atlas-ServoBench 1.2.5** profile. Ordinary Bringup still cannot enable PWM. Both profiles inhibit pyro, RGB and the flight-control hook. No servo is enabled by connecting, probing sensors, opening a tab, selecting a preview angle, or updating firmware.

## Physical setup

The exact [KST X10 V8.0 specification](https://www.kst-servo-shop.de/media/44/69/96/1749814229/KST_0106_X10_V8_Datenblatt_04_2025_de.pdf?ts=1751463833) gives 4.8–8.4 V operation, 1520 µs / 333 Hz control and a 900–2100 µs signal range. Its default travel is ±50°: the position table maps −50°/0°/+50° to 1000/1500/2000 µs. The dashboard uses that nominal position mapping, not the larger electrical signal range. Check the exact part, polarity and connector before connecting: brown negative, red supply, orange PWM. Verify logic levels and waveform with a scope; KST specifies at least 3.3 V for HIGH. A commanded pulse does not prove a horn angle or electrical waveform.

At the owner's request, **1.2.4 uses 8.55 V as the only ServoBench voltage ceiling**, replacing the former 8.3 V setting. A usable PWM-supply ADC sample from 1 through 8550 mV is accepted; 8551 mV stops PWM. Zero, invalid/stale PWM samples and ADC/reference/monitor failures still block or stop output. Separate low-voltage, 3V3-band, arm-feed and DMM-agreement gates were removed from this profile. A missed normal-policy check in the 1.2.2 owner loop still cut an active output above 8.4 V. Version 1.2.4 isolates the ServoBench policy in the full task loop; a regression executes that loop before and after the correction. Normal flight voltage/permission policy is unchanged.

This owner-selected cutoff is above KST's published 8.4 V maximum. It does not change the servo rating or qualify the supply, ripple, overshoot or GPIO waveform. The measured 8.42 V rail now satisfies the software ceiling. Stopping PWM does not disconnect servo supply power.

Power off before attaching/removing a servo. Keep J5 open, pyro loads disconnected, and all horns/linkages unrestricted. Start with one disconnected-load PWM channel on the scope, then one free servo. Keep a physical battery disconnect available: stopping the signal does not remove servo power and some servos retain their last position.

## ADC averaging in 1.2.4

An unloaded 1.2.3 test stopped at a captured 8559 mV ADC sample. The owner explicitly chose short ADC averaging while keeping 8.55 V. ServoBench now averages 16 conversions per rank in ADC1 and ADC3 hardware, then divides the sum by 16 (right shift 4). The result preserves the existing 16-bit scaling and factory-reference calculation. The 810.5-cycle acquisition time and all ADC/freshness/failure checks remain. Normal and ordinary Bringup acquisition are unchanged.

This reduces measurement noise; brief peaks are judged through the averaging window. It does not prove a ripple-free rail or peak-voltage compliance. The full 10-rank external scan takes several milliseconds; it is completed asynchronously within the unchanged 20 ms timeout. There is no long software smoothing window or automatic re-enable after a trip. This uses the hardware averaging described by [ST AN5354, section 2.2](https://www.st.com/resource/en/application_note/an5354-getting-started-with-the-stm32h7-series-mcu-16bit-adc-stmicroelectronics.pdf) and the bundled H743 HAL's ratio-minus-one register conversion. The voltage table still accepts 8550 and rejects 8551 mV, now applied to an averaged sample.

## Dashboard procedure

1. [Build and update](GROUND_STATION.md#later-updates-without-boot0nrst) using **Build Atlas ServoBench.cmd**, then select **ServoBench** in Firmware. Verify the connected profile reads `servo_bench` 1.2.5. All outputs initially remain low.
2. Open **Servo workbench**. Select the matching PCB label 1–8: **PWM 1 is at the right; PWM 8 is at the left.** Models display nominal selected and MCU-commanded angles; they do not measure shaft position.
3. Confirm unrestricted horn/linkage movement, J5 open and pyro loads disconnected, then choose **Enable PWM**. This dashboard enables the full requested default travel window of **1000–2000 µs**, corresponding to **−50° to +50°**. The existing firmware starts at 1520 µs, shown accurately as nominal +2°. Selecting **0°** sends 1500 µs.
4. Click **−50°, −25°, 0°, +25° or +50°** for a single move. For any other whole-degree angle, type it and press **Move to angle** or Enter. Typing alone changes only the preview. One selection sends one bounded target; no drag stream, retained target queue, retries or keepalive is used. Off/demo selections cannot enable hardware. A session created by another client with narrower pulse limits cannot be exceeded.
5. The models show amber for the local selection and green for the MCU's applied pulse. The firmware writes each destination directly; the KST controller performs the motion at its native response speed. There is no firmware speed ramp. At three seconds without another accepted move, PWM turns off; explicitly enable again for another move. Stop remains immediate. Servo power remains connected, so disabling PWM does not guarantee that the shaft releases.
6. Choose **Stop all PWM** before changing channel or hardware. Verify the reported mask and pulse return to zero. A tab exit/hide attempts a stop; the firmware timeout remains independent of the browser. The servo page reads compact current snapshots every 40 ms; full sensor history and events remain in their usual tabs.

## Independent firmware limits

| Condition | Behavior |
|---|---|
| One channel active | A second enable is rejected; stop first |
| No accepted position update for 3 seconds | PWM off |
| 30 seconds since enable, even with updates | PWM off; another explicit enable is required |
| USB/DTR loss or USB session change | PWM off |
| Invalid/stale PWM ADC or reference, PWM = 0 or above 8.55 V, ADC/monitor failure | Enable rejected or active PWM cut off |
| Watchdog/output fault | PWM off and fault state retained |
| Stop during a busy command, stale/blocked host or queued enable | Immediate register/pin deassertion; cancellation epoch prevents accepted older commands from re-enabling |

The output owner checks limits every 5 ms, independently of console progress/result-queue space. Its existing watchdog covers owner stalls; this is not an independently certified power cutoff. Position commands are limited to 50 ms in the IO queue. Browser/host gates add identity, freshness and explicit-enable checks, but the MCU independently enforces PWM voltage/USB/timing limits.

TIM1/TIM3 use a 1 MHz counter and 3003-count frame (about 333 Hz). New enables latch 1520 µs before exposing the pin. Each accepted position request writes the destination compare once. The enabled output uses compare preload, so the new pulse starts at the next PWM period without resetting the counter, forcing an update event, or restarting the output. This matches the direct destination behavior that the owner found smooth on enable, while preserving the running waveform. The servo’s internal controller handles movement; the former 600 µs/s software ramp is removed in 1.2.5. An accepted move refreshes only the 3 s idle deadline, never the 30 s session deadline. `target_us` and `pulse_us` now report the same accepted destination; neither measures shaft angle. ServoBench uses a 100 ms status scheduling interval and 1 ms USB task period; measured delivery rate depends on transport load. Stop retains its reason and last PWM ADC sample. The ordinary flight PWM/pyro policy remains disabled in this profile.

| PCB label (right → left) | Header | MCU pin | Timer output | Old schematic net |
|---|---|---|---|---|
| 1 · far right | J19 | PB0 | TIM3 CH3 | PWM7 |
| 2 | J20 | PB1 | TIM3 CH4 | PWM8 |
| 3 | J21 | PE9 | TIM1 CH1 | PWM1 |
| 4 | J6 | PE11 | TIM1 CH2 | PWM2 |
| 5 | J15 | PE13 | TIM1 CH3 | PWM3 |
| 6 | J16 | PE14 | TIM1 CH4 | PWM4 |
| 7 | J17 | PC6 | TIM3 CH1 | PWM5 |
| 8 · far left | J18 | PC7 | TIM3 CH2 | PWM6 |

This mapping joins the candidate manufacturing placement/copper exports in [hardware/manufacturing](../hardware/manufacturing/README.md) with schematic page 9 and the owner's observed rightmost connector → old PWM7. It is not a simple reversal. All public PWM channel indices and masks now follow PCB labels; generated Core/.ioc pin names still identify schematic nets. Existing normal-application calibration entries must be associated with physical labels when eventually qualifying that profile. One common route table governs pin mux, compare writes and shutdown.

The dashboard/host require `servo_direct:true`, the corrected layout, 16-sample ADC metadata and the 8550 mV ceiling before enabling or moving. Older firmware telemetry remains viewable and Stop remains available. A host control epoch and connection generation reject HTTP commands delayed across Stop, a new enable or reconnect.

## Acceptance boundary

Host tests exercise all eight mappings, range/neutral checks, timeout wraparound, repeated-command session expiry, USB epochs, the 8550/8551 mV boundary, zero/invalid/stale PWM and ADC/reference faults and stop-before-queued-enable. They prove software behavior against models, not physical waveform or servo travel. Browser demo angle selections are validated separately and send no hardware commands.

See the [1.2.5 servo review](SERVO_FIX_REVIEW.md) for current physical-test results. Waveform, all-connector and travel qualification require measurements on the assembled board. Accepting a voltage in software is not a hardware qualification. Pyro qualification remains outside this workbench. Record scope/meter values, exact servo, pulse limits, travel direction, neutral and cutoff behavior before expanding the accepted operating envelope.
