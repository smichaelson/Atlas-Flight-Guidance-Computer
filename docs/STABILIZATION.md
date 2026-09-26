# Four-servo bench stabilization (ServoBench 1.3.4)

This mode is for the owner's fixed bench demonstration. It runs in Atlas firmware, using the LSM6DSV16B accelerometer and gyroscope. USB and dashboard polling are not part of the control loop. Ordinary Bringup still inhibits PWM, and all bench profiles keep pyro inhibited.

## Layout and motion

View clockwise azimuths looking down from the USB-C end, with the STM32 face at 0°. All shafts point radially outward. Public channel numbers are the physical PCB labels, using the corrected routing table from [ServoBench](SERVO_BENCH.md).

| PCB PWM | Azimuth | MCU / timer |
|---|---|---|
| 7 | 45° | PC6 / TIM3 CH1 |
| 8 | 135° | PC7 / TIM3 CH2 |
| 1 | 225° | PB0 / TIM3 CH3 |
| 2 | 315° | PB1 / TIM3 CH4 |

Each horn rests down at nominal 0° / 1500 µs. The controller projects world-down into that horn's rotation plane and commands the nearest reachable angle, bounded to 1000–2000 µs (nominal ±50°). All four start at 1500 µs. Version 1.3.4 follows changing destinations every 5 ms (200 Hz), matching the cadence of the observed native sweep. It sends the latest geometric destination directly, without a software speed limit, extra position filter, or generated ramp.

Each shaft has its own rest detection. From rest it begins tracking when the target differs from the last actual command by 6 µs / 0.6°. During tracking, even single-microsecond changes pass through. If the target stays within 3 µs / 0.3° of its retained reference for 150 ms, small changes are held again. Slow movement accumulates relative to the actual command, so it cannot accumulate a pointing error of 0.6° or more from this hold alone. Very slow tilts can still make small discrete corrections; physical response remains subject to observation. This replaces the unsuccessful 100 ms / 5° experiment in 1.3.2–1.3.3.

Exact endpoints remain reachable through a final correction smaller than the rest threshold. A singularity holds the actual current command and clears that shaft's tracking state. A delayed iteration considers only the latest target; it never replays missed corrections. Targets use timer compare preload; unchanged compares are not rewritten, and running updates never reset the PWM counter. The approximately 333 Hz electrical PWM frame and 8.55 V software ceiling are retained. The owner confirmed that all four servos are KST X10 V8.0.

A radial single-axis servo cannot point exactly down for every 3D attitude. When gravity is along its shaft there is no unique horn angle; the previous command is held and marked as a singularity. Travel saturation is reported separately. A 2° band around the inverted atan2 branch cut retains the previous end of travel to avoid chatter. These are commanded angles, without horn position feedback.

The default pulse direction assumes increasing width turns the horn clockwise viewed from outside its shaft. This is a default convention; use the per-channel Reverse direction settings to match the actual servos.

On the connected bench, the owner observed all four turning counterclockwise for increasing pulse width. All four Reverse direction settings were therefore saved (mask 15). Horn illustrations honor that saved direction; numeric angle readouts retain the manual workbench's pulse convention.

## First setup

1. Build with **Build Atlas ServoBench.cmd** and update the board using Firmware → ServoBench. Check version **1.3.4** for continuous target updates with a small rest window. See the review record for physical acceptance status.
2. Keep the installed FAT SD card in place. Turn SW2 OFF. Hold Atlas still with USB-C up and the horns mounted down at nominal 0°.
3. Open **Stabilization**, acknowledge the fixed bench/free-motion setup, and choose **Calibrate upright & save**. At least one second of stationary IMU samples is required. This records the rest gravity vector and gyro bias without moving any servo.
4. With stabilization disabled, use small manual moves in Servo workbench to verify each channel and its direction. Set any Reverse direction checkboxes and **Save directions**. Calibration/direction saves leave stabilization disabled.
5. Turn on **Enable stabilization**. Wait for a verified save. Hold SW2 OFF at least 100 ms, then turn SW2 ON. Once IMU and rail checks are healthy, the four horns follow down.
6. Turning SW2 OFF stops PWM. It preserves the saved enable setting. Turning the dashboard toggle OFF stops immediately and requests a persisted disable. **Stop all PWM** stops immediately without changing the saved enable setting.

The dashboard can be closed and USB disconnected during operation. After a power cycle, retain the same SD card and cycle SW2 OFF then ON. Starting with SW2 already ON does not cause motion. A sensor/power trip or Stop also requires that physical cycle. Stopping PWM does not disconnect servo power.

## Estimation and ownership

The sensor owner automatically initializes the direct IMU in ServoBench, even without USB. New accel/gyro samples are copied to the output owner. A unit gravity vector is propagated with gyro Rodrigues rotation, corrected toward normalized acceleration with a 0.5 s time constant. Bias comes from the upright calibration; no magnetometer/yaw heading is needed.

The output owner runs every 5 ms, including switch, freshness and power checks. It rejects stale samples beyond 100 ms, nonfinite data, rates over 500°/s, and implausible acceleration. Acceleration corrections require magnitude 0.85–1.15 g. More than 250 ms without a usable gravity correction removes readiness. Startup settling takes at least 250 ms. This is a slowly moved bench demonstration; sustained linear acceleration is not distinguishable from gravity with this estimator.

Manual PWM and autonomous stabilization are mutually exclusive. Sensor maintenance and DFU are blocked while the saved mode is enabled. Shared Stop fences older queued manual commands and configuration saves. USB session changes continue to stop manual PWM but leave autonomous operation alone. Stale ADC/reference, zero/over-ceiling PWM rail, output-monitor and watchdog faults retain their existing inhibits. No automatic fault re-enable occurs while SW2 stays ON.

Version 1.3.3 retains the precise estimator failure as `stabilization.fault_detail` when an active run stops for reason 7. Codes identify invalid samples (1), stale data (2), excessive rotation rate (3), implausible acceleration (4), invalid vector (5), loss of gravity correction (6), settling (7), geometry (8), command validation (9), and nonfinite data (10). The detail survives subsequent good samples while stopped and clears on a successful restart or configuration application. The dashboard displays the matching description. The validity thresholds are unchanged.

## Persistence

The storage owner alone reads/writes the dedicated root file **ASTAB.CFG**. It contains a versioned 64-byte little-endian IEEE-754 record: magic, version, enable flag, four reversal bits, upright vector, gyro bias, zero reserved fields, and CRC32 over the first 60 bytes. Saves require exact write, sync, close, reopen, exact length, byte comparison and CRC validation before the output owner applies them. Generic append cannot modify this filename, including mixed-case aliases.

ServoBench reads the record once at boot, then unmounts. There are no boot writes, flash/option-byte writes, formatting or retries. Missing media or invalid settings means disabled. Keeping the card inserted is required for persistence across power cycles; an already loaded running mode uses RAM. A failed or interrupted SD write has an uncertain durable outcome. The UI reports failure, runtime outputs stop, and the next boot still requires SW2 OFF then ON. Never interpret an HTTP acceptance as a verified save; use telemetry and the command result.

## Verification

See [the three-pass review and evidence](STABILIZATION_REVIEW.md). Software models cover geometry, bias/rotation, freshness/wrap, corruption, save failures, switch gating, USB independence and output failures. Electrical waveforms, actual direction, mechanical travel and response under physical tilting require observation of the assembled bench.

On the connected 1.3.4 bench, the owner reported smooth, responsive tracking and confirmed operation both without USB and after a battery-only cold restart. Reconnected telemetry verified the persisted enable/calibration/directions and final SW2 OFF / PWM stopped state. Residual servo crackling remains audible and its cause is unconfirmed.

Primary references: [ST LSM6DSV16B datasheet](https://www.st.com/resource/en/datasheet/lsm6dsv16b.pdf) for sensor axes/units; the pinned driver explicitly decodes this part's Z/Y/X direct accelerometer register order. [KST X10 V8.0 specification](https://www.kst-servo-shop.de/media/44/69/96/1749814229/KST_0106_X10_V8_Datenblatt_04_2025_de.pdf?ts=1751463833) provides the nominal pulse/travel mapping, subject to physical commissioning. The owner-selected 8.55 V cutoff is documented in [ServoBench](SERVO_BENCH.md); it does not change the servo's rating.
