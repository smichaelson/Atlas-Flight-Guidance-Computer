# Startup — Atlas Rev. 0.1 PCB bring-up

Purpose: take an **assembled STM32H743ZIT6 Atlas board** from unpowered inspection to measured, logged peripheral operation. This is the dedicated bench procedure; you do not need to read ten device guides first.

Major functions covered: build the inhibited diagnostic image; enter STM32 factory USB DFU; program and verify flash; connect the local dashboard; test power, GPIO, buzzer, sensors, RTC, SD, BLE and GNSS; diagnose failures; preserve evidence. Optional external-bus/radio tests come last. **RGB, PWM and pyro actuation are unavailable in this image; the confirmed rev-0.1 Q6-Q8 LED defect is held low in software.**

The software has build/model-test evidence, not measurements from your PCB. Stop at an unmet physical gate. A responding sensor, a green row, or an `OK` reply is not electrical, calibration, deployment or flight qualification.

## Route through this guide

1. [Prepare and build offline](#1-prepare-and-build-offline).
2. [Inspect wiring while unpowered](#2-unpowered-inspection-and-pin-map), especially [USB polarity](#usb-data-polarity-is-a-required-as-built-check).
3. [Verify the power rails](#3-first-power--no-usb-no-card-no-external-loads).
4. [Program over USB DFU](#4-program-the-stm32-over-usb-dfu).
5. [Open the dashboard](#5-connect-the-laptop-dashboard), then [test the initial systems](#6-initial-tests-in-order).
6. [Optional fixtures](#7-later-stage-logic-fixtures-and-radio), [shutdown/recovery](#8-shutdown-soak-and-recovery), and [acceptance record](#10-acceptance-record).

## 1. Prepare and build offline

Keep the PCB unpowered while installing tools and building. Required bench equipment: current-limited DC supply, DMM, suitable insulated jumpers/probes, a USB **data** cable, ESD-safe work surface, and a spare backed-up FAT32 microSD card. An oscilloscope/logic analyzer is needed to accept ripple, timing and pin waveforms. Have an ST-LINK/SWD recovery option available if direct USB cannot enumerate; a bad USB path cannot be repaired by USB software.

### Laptop tools

- VS Code with your STM32CubeIDE extension and its CMake/Arm GNU toolchain. Open **the repository root**, containing `CMakeLists.txt`, not `Core/` or `App/`.
- Install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html), including its Windows USB DFU driver. It performs direct-ROM USB programming. The IDE's usual ST-LINK debug launch is a different connection.
- Python 3.10+ with Tk; Python 3.12 was used for the offline dashboard tests. `python -m tkinter` should open a small test window. The dashboard uses only Tk, standard-library code and pinned pyserial 3.5.

Create a project-local Python environment from the repository root (PowerShell). No activation or execution-policy change is needed:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/bringup/requirements.txt
.\.venv\Scripts\python.exe tools/bringup/dashboard.py --demo
```

`--demo` is clearly marked **SIMULATED**, never opens a port, and does not execute tests. Close it before real bring-up. Creating a virtual environment keeps dependencies local; the real serial connection uses nonblocking reads and a bounded write timeout. [Python venv](https://docs.python.org/3/library/venv.html), [pyserial API](https://pyserial.readthedocs.io/en/latest/pyserial_api.html).

### Build the correct image

In the VS Code Command Palette, use **Tasks: Run Build Task → Atlas: build inhibited bring-up**. The supplied task configures and builds `Bringup`. Alternatively select the **Bringup** configure/build preset through the CMake/STM32 extension. ST documents the preset-based CMake workflow in its [VS Code CMake guide](https://dev.st.com/stm32cube-docs/stm32cubeide-vscode/latest/en/docs/markup/basic_concepts/cmake.html).

Equivalent commands in a terminal where the extension's tools are available:

```powershell
cube-cmake --preset Bringup
cube-cmake --build --preset Bringup --parallel 4
```

With standalone CMake/Ninja/Arm GNU on `PATH`, replace `cube-cmake` with `cmake`. If the shell cannot find the extension wrapper, use the extension's terminal/tool environment or the standalone workflow; do not change the target to a host compiler. **Do not regenerate `Atlas.ioc` for this procedure**: manual USB, filesystem, memory and RTOS integrations are already present.

If Ninja is unavailable or hangs, use a **new, separate** directory and installed GNU Make:

```powershell
cmake -S . -B build/BringupMake -G "MinGW Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake" "-DCMAKE_BUILD_TYPE=Debug" "-DATLAS_BRINGUP=ON"
cmake --build build/BringupMake --parallel 4
```

Supply `"-DCMAKE_MAKE_PROGRAM=<your installed gmake.exe path>"` if necessary. Do not reuse a Ninja cache for Make. The software review used this Makefiles fallback; it did not operate your VS Code UI or flash your board.

| Output in `build/Bringup/` (or your chosen build directory) | Use |
|---|---|
| `Atlas-Bringup.hex` | **Select this file in CubeProgrammer.** Addresses are embedded, beginning at `0x08000000` |
| `Atlas-Bringup.bin` | Equivalent raw image; only if explicitly programmed at `0x08000000` |
| `Atlas-Bringup.elf` | Symbols for SWD/debug and inspection; includes the complete linked application |
| `Atlas-Bringup.manifest.json` | Profile, part, build type, sizes and SHA-256 hashes |
| `Atlas.map` | Linker memory/section evidence; the CMake target remains named `Atlas` |

Verify the generated files before selecting one for programming:

```powershell
.\.venv\Scripts\python.exe tools/bringup/image_check.py build/Bringup/Atlas-Bringup.manifest.json
```

Use `build/BringupMake/...` instead if you used the fallback. Require `verified_offline: true`, `profile: bringup`, `target: STM32H743ZIT6`, and `flash_base: 0x08000000`. This checks hashes, vector table and HEX/binary agreement, and rejects writes outside flash bank 1. It does **not** verify an attached MCU. Save the manifest with your bench record. Never select `Atlas.hex` from a normal Debug/Release build by mistake.

No separate application bootloader, RTOS image, sensor firmware blob or BLE/GNSS reflash is required. The factory STM32 ROM bootloader is already in the MCU; the generated HEX contains the Atlas application, kernel and linked drivers. `Atlas.ioc`, C files, dashboard Python and documents are **not** files to push to flash.

## 2. Unpowered inspection and pin map

Remove all power, USB and debug connections; let capacitors discharge. Keep **J5 open** and **J10–J14 pyro loads disconnected**. Leave motors/servos, J9 radio and all external loads disconnected. Remove the microSD initially. Never use a resistance/continuity range on a powered board.

Check assembly revision/order records against the [schematic PDF](../hardware/Atlas-schematic-rev-0.1.pdf) and manufacturing exports. Inspect orientation, solder bridges, missing parts and MCU marking. Check each rail against ground for a persistent short; capacitance can cause a transient low reading, so there is no universal acceptable resistance. Resolve suspicious readings before power. Confirm connector orientation by **pin numbers and continuity**, not by assuming a top/bottom drawing orientation.

### Power, boot and reset points

| Function | Rev. 0.1 connection | What to do |
|---|---|---|
| Main input | **J1 pin 2 VIN_RAW positive; pin 1 GND** | Current-limited DC input; confirm physical polarity |
| Ground reference | TP2 / circuit GND | DMM negative and logic instrument ground |
| Raw/protected input | TP1 VIN_RAW; TP3 VIN_PROT | Measure both sides of the main eFuse |
| Regulated rails | TP4 8V4_PWM; TP5 3V3_SYS; TP6 3V3_AUX; TP7 5V_SYS | Independently measure before USB/peripheral tests |
| STM32 BOOT0 | **TP8, U6 pin 138**; R12 10 kΩ to GND | LOW for application. HIGH to board 3V3_SYS for factory-ROM selection under normal boot options |
| STM32 NRST | **TP13, U6 pin 25**; R16 10 kΩ to 3V3_SYS, C53 to GND | Momentarily connect to GND to reset; release to let its pull-up raise it |
| Physical pyro arm | **J5** | Leave OPEN throughout this procedure; this feed bypasses the main eFuse |
| GNSS SAFEBOOT | TP12 | **Not** STM32 BOOT0. Do not connect or toggle it for STM32 programming |

The STM32's BOOT0/NRST package assignments and power/boot functions are also in the [STM32H743 datasheet](https://www.st.com/resource/en/datasheet/stm32h743zi.pdf). No extra external USB pull-up or BOOT1 jumper is required by this board procedure. PDR_ON/power pins are board-wired, not operator mode controls. Never apply USB 5 V to BOOT0, NRST or a 3.3 V logic pin.

For BOOT0 HIGH, an insulated jumper from **TP5/3V3_SYS to TP8 through about 1 kΩ** is a convenient current-limited connection; the existing 10 kΩ pull-down remains. Verify the resulting HIGH voltage. Set/remove that jumper **with power off**. Use a separate momentary NRST-to-GND connection, held for at least 10 ms when deliberately resetting. Do not short a rail to ground while probing adjacent pads.

### USB data polarity is a required as-built check

The schematic's J2-to-U16 drawing crosses the D+/D− net assignments, while the exported copper pad attributes identify the expected polarity. This is a documentation/manufacturing conflict, **not proof that your assembled board is wrong or right**. Before USB use, verify the actual paths with an unpowered, suitable continuity measurement:

| USB receptacle J2 | Expected protection path | STM32 endpoint |
|---|---|---|
| **A6 and B6: D+** | U16 I/O1, pins 1 ↔ 6 in the manufacturing pad assignments | **PA12, U6 pin 104: USB D+** |
| **A7 and B7: D−** | U16 I/O2, pins 3 ↔ 4 in the manufacturing pad assignments | **PA11, U6 pin 103: USB D−** |

Use accessible pads/test fixtures; avoid bridging fine MCU leads with a large probe. Also check D+/D− are not shorted to each other, ground or power. U16's I/O pin pairs are specified in the [USBLC6-2 datasheet](https://www.st.com/resource/en/datasheet/usblc6-2.pdf). Its VBUS support-pin wiring also warrants as-built protection review: the schematic does not show it tied to USB VBUS. Firmware does not qualify ESD protection.

If polarity is reversed, stop USB commissioning and resolve the hardware discrepancy with the board designer; use correctly wired SWD for diagnosis. The fixed USB PHY pins cannot be repaired by swapping software GPIO labels. Do not alter the preserved manufacturing exports or make speculative PCB cuts.

## 3. First power — no USB, no card, no external loads

USB VBUS is a **detect input**, not the source for Atlas system power. Connecting only USB is not a valid power-up method. Rails are enabled by hardware; withholding firmware probes does not physically isolate the powered chips or the 8.4 V connector supply.

The schematic notes an input maximum of **16.3 V**. Do not assume a fully charged 4S pack (16.8 V) is acceptable. R32=133 kΩ / R33=13.7 kΩ and the TPS1685x typical 1.21 V UVLO threshold imply approximately **12.96 V rising input threshold**. Thus a nominal 12 V supply may never enable the board. A provisional **15.0 V** bench input is suitable only after confirming these as-built parts and ratings. This threshold calculation follows the [TPS1685x datasheet](https://www.ti.com/lit/ds/symlink/tps1685.pdf); it is not a measured threshold or board power certification.

1. With the supply output OFF, confirm J1 polarity; connect ground then positive. Keep J5 open and all loads/USB/card disconnected. Hold **NRST LOW** so unknown flash cannot execute. Leave BOOT0 LOW for this rail-only check.
2. Set 15.0 V and a **100 mA initial current limit** for a brief unloaded, reset-held inspection. This is a conservative screening ceiling, **not a validated operating/inrush budget**. Turn on and watch current immediately. If the supply stays in current limit, a rail cycles/collapses, or anything heats/smells abnormally, switch OFF and investigate. Do not repeatedly raise the limit to make a fault disappear.
3. Measure TP1 and TP3 relative to TP2, then TP5/TP6, TP7 and TP4. Initial screening targets: 3V3 rails **3.20–3.40 V**, 5 V rail **4.85–5.15 V**, PWM rail **near nominal 8.4 V**. These are diagnostic screening windows, not substituted component specifications. Stop on unexplained deviation, overshoot, oscillation or excessive heating. Measure ripple/turn-on overshoot with a scope before accepting regulation.
4. Record actual input voltage/current and every rail. Inspect module-side supplies after their ferrite beads as well. J7/J8 are hardware-enabled 5 V auxiliary outputs; J22/J23 are 3.3 V auxiliary outputs; each has **pin 1 GND, pin 2 output**. Check them with a DMM without attaching a load. Their load/current-limit qualification remains separate.
5. Power OFF. After rail checks and an input-power budget review, **250 mA at 15 V is a provisional next bench ceiling** for unloaded MCU/peripheral work. It is not guaranteed sufficient for every card/module/inrush condition. A limit event still requires investigation and a measured budget before any increase. Do not substitute a battery for current-limited bench power at this stage.

The PWM rail is live even while PWM signals are disabled. Its nominal 8.4 V is already the KST X10 V8.0 upper supply limit: measured tolerance and overshoot must be resolved **before any servo is connected**. This guide does not authorize motor or pyro power tests.

## 4. Program the STM32 over USB DFU

### Understand the two USB devices

| MCU state | Laptop sees | Tool |
|---|---|---|
| Factory system-memory bootloader | STM32 USB DFU, ordinarily VID:PID **0483:DF11**; **not COM** | STM32CubeProgrammer USB connection |
| Atlas diagnostic application | `Atlas PCB Bringup`, development VID:PID **0483:5740**, CDC serial/COM | Dashboard or one serial terminal |

Direct ROM USB DFU programs flash; it does not supply SWD breakpoints, live memory debugging or a console. ST's [AN2606](https://www.st.com/resource/en/application_note/an2606-introduction-to-system-memory-boot-mode-on-stm32-mcus-stmicroelectronics.pdf) describes the H74xxx/H75xxx bootloader and Pattern 10 boot selection; [AN3156](https://www.st.com/resource/en/application_note/an3156-usb-dfu-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf) describes its DFU interface. The development USB identifiers are not a production VID/PID allocation.

### Enter DFU and write the reviewed image

1. Close the dashboard/serial terminal. Power OFF, USB disconnected, J5 OPEN, all pyro/motor/radio loads disconnected. If existing flash contains anything worth retaining, back it up in CubeProgrammer **before** programming; this operation replaces the application.
2. Install the BOOT0-to-3V3 jumper described above. Hold NRST LOW.
3. Turn on the already-checked current-limited main supply. Recheck rails. Connect the USB **data** cable to J2 and the laptop; then release NRST. BOOT0 must be HIGH when reset is released. If needed, give another deliberate NRST LOW/release pulse while keeping BOOT0 HIGH.
4. Open CubeProgrammer. Select **USB**, refresh the device list, and choose the matching DFU device/serial number. Connect. Confirm the STM32H743 family and expected 2 MiB flash device, and record the silicon/bootloader revision. If multiple STM32s are attached, disconnect the unrelated ones or explicitly match the intended serial number.
5. In the file programming view, select **the exact `Atlas-Bringup.hex` whose manifest you checked**. Enable programming verification. Leave automatic application run/“Run after programming” OFF. Do not select mass erase, readout-protection changes, option-byte programming, or an unrelated memory range. Allow the programmer's necessary sector erase for this image; the HEX is bounded to bank 1.
6. Start programming and require both successful download and **successful verification**. Save the log and the manifest. On failure, leave loads disconnected, record the error, and diagnose; do not assume partial flash is usable. No automatic flash command is run by the repository's build tasks or dashboard.
7. Disconnect in CubeProgrammer. Unplug USB, switch main power OFF, and confirm rails have discharged before moving jumpers. Remove the BOOT0 HIGH jumper; R12 returns BOOT0 LOW. Release any temporary NRST hold for normal operation.
8. Turn main power ON with BOOT0 LOW and J5 still OPEN; check rails/current again. Attach USB after stable power. This time expect the **CDC COM device**, not DFU. Continue to the dashboard.

Use a genuine power cycle after DFU, not a software “Go” shortcut. H743 ROM revisions differ; older ROM behavior can leave power configuration locked until power-off. Boot selection also depends on option bytes: the expected normal mapping is flash at BOOT0 LOW and system memory at BOOT0 HIGH (Pattern 10 uses `BOOT_ADD1=0x1FF0` for that HIGH case). If these options were changed, inspect them with an appropriate programmer/SWD; **do not blindly rewrite them or disable protection**. AN2606 also documents revision-specific behavior involving PB15; do not force MCU RX (PB15, driven by GNSS TX) LOW to enter DFU.

### If direct USB cannot work

First confirm main power, BOOT0/NRST levels, data cable, Windows DFU driver, the polarity check above, and the actual fitted part. A DFU driver problem and a missing CDC driver/COM port are different problems. Application USB also needs the generated clock setup to succeed, including the fitted **8 MHz HSE and 32.768 kHz LSE**; a clock/startup failure can occur before any USB report exists.

Use SWD for diagnosis/recovery when necessary. **J28 is a custom connector, not a standard keyed Cortex-debug header**:

| J28 pin | Signal | ST-LINK connection |
|---|---|---|
| A1 | 3V3_SYS | Target reference/sense (VTref), not an unreviewed power feed |
| A2 | SWDIO | SWDIO |
| A3 or A5 | GND | GND |
| A4 | SWCLK | SWCLK |
| B5 | NRST | NRST for connect-under-reset |

The other J28 B pins are general GPIO, not debug pins. Verify cable orientation and continuity first. Power Atlas from its reviewed main supply; do not also feed the rail from the probe. For a crashing application, use connect-under-reset at a modest SWD rate and the matching ELF. ROM DFU availability cannot be guaranteed in the presence of wiring, option-byte, crystal, power or silicon faults.

## 5. Connect the laptop dashboard

From the repository root:

```powershell
.\.venv\Scripts\python.exe tools/bringup/dashboard.py
```

1. Select **Refresh ports**, choose the Atlas application's COM port explicitly, then **Connect**. No port opens automatically. Only one terminal/dashboard may own it.
2. Opening asserts CDC DTR and should produce a schema-1 `hello`: profile `bringup`, version `1.0.2`, both `pwm_pyro_inhibited:true` and `led_inhibited:true`, device UID and nominal CPU 200 MHz. The updated dashboard rejects an older image that can still command RGB high. **Identify** requests the handshake again. Never proceed on an unrecognized profile. `hello`/`status` requests exercise both USB directions without probing sensors.
3. The status stream is nominally 2 Hz. Initially the sensor rows should be NOT TESTED; analog/input monitoring is already running. Lack of GPS fix, missing radio/card, and unprobed devices are not startup failure by themselves.
4. Tick the power/load checklist only after the physical steps above. Buttons issue one operation at a time. ADXL, LSM, MMC, barometer, BNO, BLE and radio probes are permitted **once per MCU boot**, including a failed attempt. GNSS is the sole exception: after a completed failure, another explicit click safely stops/flushes its UART and retries; if identity passed but RAM configuration failed, only configuration is retried. There is no automatic retry or replay.
5. In **Raw evidence & notes**, start a **new** log if desired. Logging never overwrites a file and stops at 100 MiB or a disk error. Logs include UID, UTC and GNSS position: keep them private and out of Git. Enter DMM, scope and peer observations as bench notes. Starting a log late does not recover earlier frames.

The table distinguishes NOT TESTED, NO SAMPLES, RESPONDING, STALE, FAILED, transport initialization and reported GNSS fix. Read the actual command reply in the event log as well. “Transport initialized” for the external radio proves only that its MCU UART driver started. GPIO `outputs` are **commanded bits**, not measured connector voltages. SD “mounted” does not prove a successful write/readback. BLE AT identity does not prove RF communication.

Host telemetry older than 2.5 s is stale; commands that change device state require a fresh recognized status. Malformed frames, a detected MCU/session restart, or an uncertain command timeout latch a client block. No command is automatically replayed. A late success reply does not clear uncertainty: inspect evidence and reconnect manually. The firmware also rejects old/duplicate command IDs and drops old-session completions. An already executing SD/device operation can finish after cable removal; disconnect is **not cancellation**.

## 6. Initial tests in order

### A. Power, USB, inhibited RGB, buzzer and inputs

- Watch `power.count` and timestamps advance with valid channels. Compare reported 3V3/PWM/5V/VIN_PROT against the DMM; record both and resolve divider/reference error rather than treating ADC output as the standard. `vdda_mv` and die temperature must be plausible. AUX 3V3 and auxiliary connector outputs still require their own meter checks; there is no separate AUX ADC rank.
- Stop this acceptance section if `power.status` is nonzero or `power.count` remains zero. The 2026-09-04 version 1.0.1 evidence has exactly that separate fault (`status=IO`, zero samples, zero external-ADC errors); it does not prove bad rails and must not be conflated with the GNSS failure. Version 1.0.2 deliberately leaves all ADC conversion and acceptance policy unchanged, but adds the retained `ref_stage`, `ref_channel`, `ref_raw`, `ref_hal_status`, and `ref_hal_error` fields below. Preserve the complete new frame and DMM values before power-cycle or code changes.
- The ten ADC channels are ordered **3V3, PWM supply, 5V, VIN_PROT, ARM supply, continuity 1, 2, 3, 4, 5**. `valid` bit *i* qualifies `mv[i]`. Zero voltage is not proof of an open lead or absence of a short. With J5 open, continuity/arm qualification remains unknown and no firing inference is permitted.
- Do **not** attempt to illuminate D5. The raw-design review confirmed that each DMN3404L Q6-Q8 footprint routes the intended load/control/ground to the wrong physical gate/source/drain terminals. Require `led.inhibited=1`, `led.commanded=0`, and normally `led.gates=0`. Measure PB6/PB7/PD14 only to confirm they remain near 0 V through startup and later commands; never command or manually force them high. See the [hardware-inhibited LED guide](reference/modules/LED.md).
- Request the short beep once. Verify differential drive on PE5/PE6 and expected sound, then use **Stop / force RGB low**. The requested duration is 200 ms at nominal 4.8 kHz; its owner-serviced expiry can extend during a stalled bus operation. Do not accept timing solely from the request value.
- Toggle SW2 and observe `gpio.switch`. Input bits 0–6 represent IN1–IN7. Initial general outputs, PWM mask and software-armed flag must be zero. Scope the disconnected PWM/pyro signal/gate pins during reset/startup if accepting their passive state; this image cannot drive them HIGH deliberately.
- Press Identify and confirm a matching successful reply and increasing USB byte counters. Disconnect/reconnect once with no command pending: expect a new handshake and fresh data, never replayed tests. USB RX/TX drops and timeouts should not keep increasing in a healthy steady connection.

ADC3 reference diagnostics use the following retained stage values. Stage zero means no failure; in that case `ref_channel` and `ref_raw` describe the latest completed reference sample rather than an error. For a nonzero stage, `ref_channel=0` means VREFINT and `ref_channel=1` means the internal temperature sensor.

| `ref_stage` | First failed ADC3 operation |
|---:|---|
| 0 | None |
| 1 | Channel configuration |
| 2 | Conversion start |
| 3 | Hardware overrun flag |
| 4 | 20 ms conversion deadline |
| 5 | HAL conversion poll |
| 6 | Conversion stop |
| 7 | Raw sample outside the guarded range |
| 8 | Computed VDDA outside 2800–3600 mV |
| 9 | Computed die temperature outside −50 to 150 °C |

`ref_hal_status` is the retained HAL result (0 OK, 1 ERROR, 2 BUSY, 3 TIMEOUT), and `ref_hal_error` is the ADC3 handle error mask at that point. `adc_errors` remains the **external ADC1/DMA scan** counter. These fields localize a failure; they do not prove whether its root cause is power, clock, calibration, configuration, silicon state, or assembly.

### B. Onboard sensors — one explicit probe at a time

Wait for each reply and continuing samples before proceeding. Leave the board stationary first; then move it slowly using insulated support, without stressing connectors or introducing shorts. Treat stale samples during a long probe/maintenance operation as unavailable until fresh data resumes.

| Button / command | Evidence to collect |
|---|---|
| ADXL / `probe adxl` | Identity/configuration OK; sample count increases; XYZ acceleration changes with orientation. Gravity magnitude roughly 1 g at rest. This high-g part is coarse (driver scale 49 mg/LSB); do not expect low-g IMU resolution |
| LSM / `probe lsm` | Accel/gyro samples and INT1 counter increase; roughly 1 g at rest, gyro near its stationary bias, signed rate changes with deliberate rotation; record axes and temperature |
| MMC / `probe mmc` | Field count increases; XYZ magnetic field changes with orientation, away from steel/tools/current loops. Values display in µT. This is not a hard/soft-iron calibration |
| BARO / `probe baro` | PROM CRC/init OK; pressure/temperature update and are plausible against a local reference. Altitude/weather affect pressure; 101325 Pa is not a universal acceptance target. Do not blow liquid/moisture into the sensor |
| BNO / `probe bno` | Version 1.0.2 retains the proven seven-bit `0x4A` (STM32 HAL `0x94`) and CEVA two-interrupt header/continuation correction. Identity and all four report enables must return OK; **each** accel, gyro, magnetic and quaternion count/timestamp must advance. Require `bno.health.io_errors=0`, `protocol_errors=0`, `pending_length` returning to zero, and continuing barometer samples. Check gravity, rate sign and quaternion norm near one. Accuracy status is separate from packet reception |

Discrete sensor polling is deliberately slower than a flight acquisition design: ADXL/LSM nominal 5 ms, MMC 100 ms, barometer 200 ms, with latest-value publication. BNO requests 100/100/50/100 Hz reports. These are scheduling requests, not guaranteed lossless sample rates. One failed module remains reported while the diagnostic owner continues servicing others; a hung task still faults supervision.

### C. SD and RTC — explicit, non-destructive file-level test

Use expendable/backed-up **FAT32** media; exFAT is disabled. The tools do not format a card. A known 4–32 GB FAT32 test card is a convenient initial choice; verify its existing filesystem on the laptop. These tests do not certify card endurance or power-loss durability.

1. With the card in a laptop reader, identify its correct drive letter. Create the exact ASCII/CRLF read fixture. **Replace `E:\` with that card's directory**:

   ```powershell
   .\.venv\Scripts\python.exe tools/bringup/card_check.py prepare E:\
   ```

   This creates `ATLAS.TXT` containing exactly `ATLAS SD READ TEST v1` plus CRLF, without BOM or extra newline. Existing files are refused. Safely eject the card from the laptop.
2. Shut Atlas down using section 8. Insert the card in J3 **unpowered**. Restart normally with BOOT0 LOW, connect dashboard, recheck rails/current. `sd.card` should be 1; `sd.mounted` remains 0 until requested. A card-detect switch is not proof of SD bus communication.
3. Click **Mount**. Require reply OK and `mounted=1`, FatFs result 0. Missing media, bad contacts or unsupported/corrupt FAT must be diagnosed, not formatted automatically.
4. Click **Read fixture**. Require OK and **23 verified bytes**. This rejects a BOM, missing/extra bytes and incorrect newline/content. It is an actual filesystem read, not a detect-only check.
5. If the laptop clock is correct, explicitly **Set RTC to laptop UTC**. Check `sd.time_valid=1` and advancing `sd.utc` in raw evidence. There is no automatic GNSS-to-RTC synchronization. VBAT is board-wired; do not assume retention without a separate measured backup-power test.
6. Click **NEW write / compare** and acknowledge the prompt. It exclusively creates **`ATLASCHK.TST`**, writes two deterministic 512-byte blocks, syncs, closes, reopens, checks length and compares every byte. Require OK and **1024 verified bytes**. A readback error or uncertain write is never retried automatically.
7. If the file already exists, the request returns an error with **FatFs=8 / FR_EXIST** and preserves it. First retain/inspect the previous result on the laptop. Archive/rename it yourself only when appropriate before a new test; there is no firmware delete command.
8. Click **Unmount**, require successful completion and `mounted=0`, then disconnect USB and switch main power OFF. Remove the card only after power is off. Reinsert in the laptop and independently compare the MCU-written file:

   ```powershell
   .\.venv\Scripts\python.exe tools/bringup/card_check.py verify E:\
   ```

   Require “1024 bytes matched.” Retain that file and the log. Do not hot-remove during a write as an incidental test; deliberate removal/power-failure qualification needs a separate expendable-media plan.

### D. BLE — host UART and RF are separate tests

1. `probe ble`: require NINA identity and firmware text, command mode and successful AT communication. Record the exact u-connectXpress version. RTS/CTS, RESET, DSR and DTR are part of this path; a UART `OK` does not validate RF.
2. Select **Volatile SPS profile**. This requests peripheral role, name **AtlasBench**, SPS server, ordinary transparent data mode selection and `AT&D1`, with readback. It does **not** issue `AT&W`, reset/save settings, or flash the module. A version that rejects a command must be investigated using the [BLE reference](reference/modules/BLE.md); do not repeatedly factory-reset the module.
3. Select **Data mode**. With a compatible u-blox **Serial Port Service (SPS)** central, find/connect to AtlasBench. Generic BLE UART services are not interchangeable with SPS; the central must implement the proper service/credit flow. Advertising alone is not a successful data test.
4. Select **Transmit fixed text**. Require the peer to receive exactly `ATLAS_LINK_TEST_1\r\n` (19 bytes). From the peer send a known short response, e.g. ASCII `ATLAS_PEER_ACK`. Confirm `ble.rx` increases by the expected amount and `last_hex` matches the bytes. `last_hex` is only the most recently drained ≤32-byte chunk, not a complete stream log.
5. Disconnect/reconnect the BLE peer and repeat a fresh, intentional exchange. Record mode transitions and any timeout/flow-control problem. Use **Command mode** before changing the profile again. Cold power loss may restore the module's stored configuration; repeat volatile commissioning explicitly as needed.

If the fitted firmware applies an advertising/service change only after persistence/restart, stop and record that compatibility requirement. The bench interface intentionally does not silently write NVM to make a profile appear successful.

### E. GNSS and PPS

With power OFF, attach a suitable GNSS antenna to **J27 SMA**, checking compatibility with the schematic's VCC_RF/L5 bias feed; do not attach an incompatible DC-short or externally powered arrangement blindly. GNSS is powered from 3V3_AUX through FB4. TP12 SAFEBOOT stays untouched.

After normal startup, `probe gnss` must return identity plus RAM-configuration/readback success. Record the MON-VER text. The default interface is USART1 PB14 TX / PB15 RX, 38400 8N1; the receiver normally emits NMEA at power-up and accepts UBX input. Version 1.0.2 first aborts/flushes stale receive/error state, arms receive-to-idle with one bounded race retry, and sends the side-effect-free MON-VER poll every 250 ms within a single 3 s identity window. Only after identity does it start PPS and request 10 Hz UBX NAV-PVT. A receiver previously commissioned to another stored baud/profile can legitimately fail this initial exchange.

On success require `init[6]=0`, `init[7]=0`, nonempty `gnss.version`, `failure_stage=0`, `failure_status=0`, `pps_started=1`, `tx_bytes>0`, and increasing `rx_bytes`/NAV frames. One `preflights` count is normal; `start_retries=1` means bytes raced the first arm and the bounded recovery was used. A navigation fix is **not** required to prove the local UART/configuration path.

On failure, preserve the entire status frame before retrying. Interpret the retained stage first:

| Stage | Meaning and next evidence |
|---:|---|
| 1–2 | UART transport registration/start. Inspect `hal_status`, hexadecimal `hal_error`, preflight/retry counts, USART1 IRQ setup and PB14/PB15 electrical state |
| 3 | MON-VER transmit failed. `tx_bytes` and a scope on MCU TX PB14 → U23 RXD pin 21 distinguish a local HAL result from a real waveform |
| 4–5 | Receive service failed or no checksum-valid MON-VER arrived within the bounded 3 s window. Compare `rx_bytes`, UART errors/drops and a 38400-baud capture on U23 TXD pin 20 → MCU PB15 |
| 6–7 | Identity succeeded, but shared timebase or PA15/TIM2 channel-1 capture startup failed |
| 8–9 | Identity succeeded, but RAM VALSET/ACK or VALGET readback failed; retain version, frame and timeout evidence |
| 10 | Runtime UART/parser service failed after initialization |

If `rx_bytes=0` while `tx_bytes` advanced, power down and verify 3V3_AUX at U23 VCC/V_BCKP/VDD_USB, common ground, TP12 SAFEBOOT_N not held low, U23 TXD-to-PB15 continuity, and idle-high/38400 traffic. If bytes arrive but stage 5 remains, check baud/framing, PB14-to-U23 RXD continuity, waveform levels, drops/errors and whether the receiver has a non-default persistent configuration. U23 reset is not MCU-routed on rev-0.1, so a true receiver cold reset requires a controlled board power cycle. Do not pull TP12 low as a recovery experiment.

After one fully completed failure, correct only safe wiring/power conditions and issue one deliberate `probe gnss` again. The retry is not automatic. A second `STATE` reply after a failed probe suggests the old 1.0.1 image or a mismatched dashboard; after complete success, `STATE` is expected because no reinitialization is needed.

Check NAV frame count/timestamps first; **valid messages with no fix indoors still prove transport**. For navigation acceptance, move to a stationary clear-sky location while maintaining a safe grounded/powered setup. Require a continuing valid 3D fix (fix type 3 or 4 with flags bit 0 set), plausible satellite count/location/height and horizontal accuracy appropriate to your test. Compare coordinates with a known reference; a fixed number of satellites alone is insufficient.

PPS is captured on PA15/TIM2 CH1. After at least two pulses, inspect the pulse count and period (nominally about 1,000,000 µs for a 1 Hz configuration) and compare to a scope. The first pulse has no meaningful interval. No PPS while no valid time/fix is not automatically a failed UART. Confirm GNSS and BNO continue together; neither may reset their shared TIM2 counter. GNSS boot configuration is RAM-only and is not saved automatically.

## 7. Later-stage logic fixtures and radio

Do this only after the initial checklist passes. **Power OFF before changing connections.** Use 3.3 V-compatible logic and common circuit ground, no external drive into an unpowered Atlas, and no connection between two driven outputs. Connector protection resistors are not level translators or permission to apply 5 V.

### General GPIO loopback

| Channel | MCU OUT | Accessible OUT | MCU IN | Accessible IN |
|---|---|---|---|---|
| 1 | PF2 | J28 B1 | PE2 | J28 B4 |
| 2 | PE4 | J28 B2 | PE3 | J28 B3 |
| 3 | PA5 | J29 A1 | PG1 | J29 B1 |
| 4 | PA7 | J29 A2 | PE7 | J29 B2 |
| 5 | PC5 | J29 A3 | PE8 | J29 B3 |
| 6 | PF14 | J29 A4 | PE10 | J29 B4 |
| 7 | PF13 | J29 A5 | PE15 | J29 B5 |

For one channel, wire its OUT to its IN and observe with a high-impedance probe. After startup and fresh analog health, press the numbered GPIO button: its logic output goes HIGH for nominally 1 s, then all seven logic outputs return LOW. Observe the matching input bit and actual pin waveform. The reply acknowledges launching the request, **not the end of the pulse or electrical success**. Use **All low** (`gpio 0`) for deliberate deassertion.

The output owner rejects stale/old-generation requests and checks USB configuration/DTR plus fresh 3V3 in 3000–3500 mV. It clears an active diagnostic pulse after the 1 s interval, on disconnect/DTR loss, a rail fault or emergency inhibition, within the next executing owner cycle (nominal 5 ms). This is task-serviced GPIO, **not an independent hard pulse cutoff**. Do not attach actuators, relays, igniters or flight controls to these tests. Scope reset/disconnect behavior before accepting the channel.

### External buses

| Fixture | Connector pin map | Test / expected result |
|---|---|---|
| UART4 local loopback | J24: 1 GND, **2 MCU RX**, **3 MCU TX** | Unpowered jumper 2↔3, no other transmitter. `uart` sends 32 fixed bytes at 115200 8N1 and compares their echo |
| External SPI loopback | J26: 1 3V3, 2 GND, **3 SCK**, **4 MOSI**, **5 MISO**, **6 CS** | With no external slave, unpowered jumper 4↔5. `spi` exchanges/compares 32 bytes in mode 3, nominal 3.125 MHz. Do not connect power to a data pin |
| Known I2C device | J25: 1 3V3, 2 GND, **3 SCL**, **4 SDA** | Enter documented decimal 7-bit address and safe 8-bit register. `i2c address register` reads one byte and reports it. There is no scan/auto-configuration; check pull-ups and device voltage first |

SPI3 is shared with LSM6DSV16B: verify the IMU CS remains inactive during the external transaction and that samples resume afterwards. An I2C read can have device-specific side effects; use a known readable identity register, not an undocumented/read-to-clear control register. A NACK with no fixture is expected, not a PCB pass.

### RFD900x at the legacy “LoRa” connector

Keep J9 absent for initial bring-up. Later, verify supply/current budget, approved local RF configuration and an appropriate antenna **before powering a transmitter**. J9 is **1=5V_SYS, 2=MCU RX / radio TX, 3=MCU TX / radio RX, 4=GND**. Confirm the exact modem variant's power and UART-level requirements; this firmware targets RFD900x/SiK, not a Semtech LoRa module.

With both boards powered safely, `probe radio` starts its 115200 8N1 UART transport. **Read identity** explicitly enters AT mode, queries identity, and attempts to return to data mode; inspect both the reply and `radio.command`. Then transmit the fixed 19-byte text only to an approved bench peer, confirm reception there, and send a known reply back. Confirm RX count/hex. No frequency/power/network-ID changes or parameter saves are exposed by this diagnostic interface. A modem with a different saved baud needs deliberate commissioning, not random baud/setting writes.

### PWM and pyro remain deferred

There is no PWM-enable, pyro-arm, fire, raw-memory or arbitrary-pin command. `ATLAS_BRINGUP=1` also rejects the normal actuator/configuration APIs and never invokes the control-algorithm hook. Do not try to turn this into an actuator test by editing one inhibit constant or attaching loads while in ROM DFU.

The normal firmware has [qualified output services](PERIPHERALS.md#output-configuration-and-qualification), but using them requires a **separate reviewed inert test application/plan**: scope all eight PWM outputs with measured KST neutral/travel and supply margin; then validate pyro channels with non-energetic fixtures, qualified ADC thresholds, physical-link transitions, 500 ms ON, ≥500 ms OFF, four-attempt ceiling, disarm/reset/stall/brownout behavior and independent cutoff assessment. Only after those gates may a responsible owner authorize real loads. Initial board bring-up does not satisfy them.

## 8. Shutdown, soak and recovery

For an ordinary shutdown: wait for the current reply, request **Stop / force RGB low** and **All GPIO low**, **Unmount** SD and verify completion, close/disconnect the dashboard, unplug USB, then turn the main supply OFF. Verify discharge before changing jumpers, cards or wiring. USB must not back-power an otherwise unpowered board through the data/detect circuitry.

After individual tests, run a supervised **10-minute initial soak** with the intended initial modules online, repeated fresh sensor/GNSS data, BLE peer exchange and explicit SD tests as appropriate. Retain current/rail/thermal readings, resets, drop/error counters and free stack words. This duration is a practical first check, not a reliability qualification. Do not leave a newly powered board unattended. Never force a brownout/short or remove a card mid-write merely to exercise an error path.

| Symptom | Check / safe next step |
|---|---|
| No rails, supply near 12 V | Verify eFuse UVLO calculation/as-built divider; USB does not supply system power |
| Current limiting, hot part, wrong/oscillating rail | Cut main power and disconnect USB; investigate assembly/polarity/short/load budget before continuing |
| No DFU device | Main power, BOOT0 sampled HIGH, NRST release, cable/driver, D+/D− continuity, part/boot options; use SWD if needed |
| DFU still appears after programming | BOOT0 still HIGH, wrong boot mapping, or flash verification failed; inspect, do not reflash blindly |
| DFU works but application CDC does not | Verify bring-up HEX/address and cold power cycle; clocks/HAL startup/faults may precede USB; inspect with SWD |
| COM opens but no telemetry | Close other clients, use real diagnostic image and assert DTR; Identify; inspect VBUS and USB counters |
| GUI says stale/blocked | Treat displayed measurements as historical; preserve log, inspect target status, reconnect manually. Do not repeat an uncertain write |
| Probe returns STATE | An ordinary module already attempted this boot, GNSS already succeeded, wrong operating mode, or another state gate. A failed GNSS is retryable only in 1.0.2. Inspect `attempted`, `init`, version/handshake, pending ID and reply |
| Sensor identity/I/O/CRC failure | Check its local supply, CS/reset/interrupt, bus pins/pulls, fitted part/address and waveform. Fix with power OFF; fresh boot before another probe |
| BNO fails and barometer stops | Retain `bno.health` and barometer counters. Version 1.0.2 should hold U12 reset and increment `recovery_attempts`; require `recovery_failures=0` and fresh barometer samples afterward. If not, power down and scope I2C1 SCL/SDA plus H_INTN; do not repeatedly probe in one boot |
| RGB field is not inhibited/zero | Stop using that image or power down on a nonzero pin readback. Version 1.0.2 must advertise the inhibit and never command PB6/PB7/PD14 high. Do not test around the confirmed Q6-Q8 defect |
| `power.status` nonzero / no samples | Preserve `ref_stage/channel/raw/hal_status/hal_error`, `adc_errors`, reset/fault fields and DMM rails. Use the stage table in section 6A; do not alter scaling or enable outputs to work around an unqualified reference path |
| SD not mounted / FatFs 13 | Check known FAT32 media; 13 is no recognized filesystem. No automatic format/recovery is performed |
| BLE responds to AT but no peer data | Verify supported SPS central, volatile profile/readback, data mode, RTS/CTS/DSR and actual module firmware |
| GNSS messages but no fix | Antenna/bias/sky view and receiver quality; do not conflate RF navigation with UART transport |
| GNSS probe fails before messages | Preserve failure stage/status, RX/TX/preflight/retry and HAL diagnostics; follow section E's power, SAFEBOOT, continuity and waveform split before retrying |
| Repeated MCU reboot | Capture reset flags and supervisor fault before reset if possible; inspect power/stack/clock/task liveness with SWD. Do not disable IWDG to hide it |

The diagnostic supervisor refreshes IWDG while console, sensor owner and successfully started output-owner tasks progress. Expected missing components do not cause the normal flight sensor-freshness reset loop. A worker command has a 40 s ceiling; liveness/low-stack/fatal faults remain fail-stop. USB and SD expose their own health/stack margins; this is not a proof of complete system schedulability. MCU hang/power loss can still defeat the dashboard or software deassertion. Physical isolation remains essential.

## 9. Protocol and implementation reference

Normal Debug/Release and diagnostic Bringup/BringupRelease are **separate compile-time profiles**, not a runtime mode switch. Source entry is [atlas_bringup.c](../App/Src/atlas_bringup.c); pure framing/formatting is [atlas_bringup_protocol.c](../App/Src/atlas_bringup_protocol.c). Dashboard code is [tools/bringup](../tools/bringup/). Owner/task details are in [RTOS](reference/RTOS.md#diagnostic-profile).

### Manual terminal alternative

Use CDC at 115200 8N1, no software/hardware flow control, DTR asserted, and LF or CRLF line endings. The CDC line baud is informational; it does not change sensor/radio UART baud. Close the dashboard first. Send **one complete line**, wait for its matching reply, and increase the nonzero decimal ID on every new command. Example:

```text
1 hello
2 probe adxl
3 status
```

Only send the next line after the previous reply; the snippet is not a batch script. There is no terminal escape sequence or raw AT passthrough. Maximum command storage is 96 bytes including termination. Malformed/overflowed lines are discarded through LF and counted; they may have no reply. After byte loss, resynchronize with a newline and a fresh manually checked connection, never by replaying a side-effecting command.

| Verb (after ID) | Meaning |
|---|---|
| `hello`, `status` | Read-only identification/ack; full status is streamed periodically |
| `probe adxl/lsm/mmc/baro/bno/gnss/ble/radio` | Choose one literal module name. Only a completed failed GNSS probe may be explicitly retried |
| `led 0`, `beep`, `stop` | Explicit RGB-low request; 200 ms nominal beep; stop buzzer and force RGB low. Nonzero LED masks are rejected |
| `gpio 1..7`, `gpio 0` | One logic-only 1 s HIGH; or all logic outputs LOW |
| `sd mount/read/test/unmount` | Choose one operation; fixed safe fixture names |
| `utc YYYY M D h m s` | Explicit Gregorian UTC, years 2000–2099 |
| `ble profile/data/command/ping` | Choose one volatile SPS/mode/fixed-text operation |
| `radio id/ping` | Identity with mode entry/exit, or fixed-text data transmission |
| `uart`, `spi`, `i2c address register` | Explicit fixtures above; I2C decimal address 8–119, register 0–255 |

Slash/range notation in this table denotes alternatives, **not literal wire syntax**. Status values: 0 OK, 1 NULL, 2 ARGUMENT, 3 BUSY, 4 NOT_READY, 5 TIMEOUT, 6 IO, 7 IDENTITY, 8 CRC, 9 PROTOCOL, 10 NACK, 11 OVERFLOW, 12 UNSUPPORTED, 13 STATE. These are Atlas statuses; `sd.fs` / reply detail separately reports FatFs codes.

### JSON data contract

Records are ASCII JSON + LF: `hello`, periodic `status`, ticketed `reply`, or explicit serialization `error`. Firmware output is bounded by 8192 bytes; the host bounds framing independently. Text is escaped, invalid floating samples become `null`, counters/timestamps are unsigned 32-bit and wrap. Compute MCU age with `(ms - t) & 0xFFFFFFFF`; never subtract a PC UTC timestamp from an MCU tick.

Important fields:

- `init[12]`: ADXL, LSM, MMC, barometer, BNO identity, BNO reports, GNSS identity, GNSS RAM config, BLE, radio transport, LED, buzzer. `attempted` bits 0–7 follow the eight `probe` names above. Before a probe, NOT_READY is expected.
- `count/errors/sample_status[4]`: direct sensors ADXL/LSM/MMC/barometer. A retained value is valid evidence only with a successful sample count, acceptable status and age. `bno.count/t/accuracy[4]` refer to accel/gyro/magnetic/quaternion separately. `bno.health` exposes transfer counts/errors, retained HAL status/error/stage/length, pending continuation length, H_INTN level, initialization and shared-I2C recovery outcomes.
- `adxl.mg`, `lsm.mg`: divide by 1000 for g. `lsm.mdps`: divide by 1000 for deg/s. `mmc.nt`, `bno.mag_nt`: divide by 1000 for µT. `bno.accel_mm_s2` / `gyro_mrad_s`: divide by 1000 for m/s² / rad/s. Quaternion `q_ppm` is **w,x,y,z** divided by 1,000,000. `temp_cc` is centi-°C; `baro.pa` is Pa.
- `gnss.lat_e7/lon_e7`: signed degrees ×10⁷; `h_msl_mm/hacc_mm`: millimetres. Interpret `fix`, `flags`, `sv`, `frames`, `t`, checksums and `pps_count/pps_us` together. `failure_stage/status`, `pps_started`, RX/TX/drop/error/restart/preflight/retry counts and HAL status/error diagnose startup. Coordinates are private test data.
- `power.mv/raw/valid/count/t` and `vdda_mv/temp_c`: analog record described above. `ref_stage/channel/raw/hal_status/hal_error` retain the first ADC3 VREFINT/temperature failure; `adc_errors` counts the separate external ADC1/DMA path. `gpio.inputs/outputs` are bit masks; general outputs are commanded, not sensed. `led.inhibited` must be 1, `led.commanded` must be 0, and `led.gates` should be 0; this only samples the legacy MCU control nets. `gpio.pwm/armed` must remain zero.
- `sd`: physical detect, mount/result/counters, RTC validity/calendar. `ble` / `radio`: identity/mode/byte counts and most recent raw chunk in hex. Neither implies peer acknowledgement.
- `usb`: session, byte/drop/timeout counters. `tasks`: heartbeats, busy/fault/parser/reply counters and free stack **words**, ordered console/owner/IO/SD/USB. `pending_id=0` means no retained command in progress.
- `reply`: `id`, numeric `status`, human-readable `name`, escaped `detail`, `verified_bytes`. A GPIO reply is request completion, an SD compare reply verifies bytes, and a radio write reply is only a local transport outcome.

The USB protocol is a **physical bench interface**, not an authenticated remote flight-command protocol. Do not bridge it to a radio/network or use the dashboard as a flight controller.

## 10. Acceptance record

Keep one record per board and image, alongside the local JSONL log. Copy this template to a private lab record; do not mark an item complete from a software build alone.

```text
Board serial / assembly revision / operator / date:
MCU marking + silicon/ROM version:
Source revision / dirty-change record / compiler / preset:
Firmware HEX SHA-256 / programming-and-verification log:
J1 polarity; J5 open; loads disconnected; USB continuity result:
Supply setting / current limit / measured input current:
DMM TP1 / TP3 / TP4 / TP5 / TP6 / TP7; module/aux rails:
Scope startup/ripple/reset traces; temperature observations:
USB DFU → application CDC / Identify / reconnect evidence:
ADC comparison / raw rank mapping / invalid-data behavior:
RGB inhibit and all-three-low evidence / buzzer waveform / SW2:
ADXL / LSM / MMC / MS5611: identities, counts, axes, reference checks:
BNO: identity, all four streams, accuracy and quaternion checks:
SD: detect / mount / 23-byte read / 1024-byte MCU compare / laptop compare:
RTC UTC advancing / retention conditions:
BLE: firmware / profile readback / SPS peer / both-direction payloads:
GNSS: version / NAV stream / sky conditions / fix quality / location / PPS:
GPIO channels actually measured / optional UART-SPI-I2C fixtures:
10-minute soak: current/rails/errors/drops/reset flags/free stacks:
RFD900x (deferred until initial gates pass):
PWM / pyro qualification: NOT PERFORMED by this image:
Failures, corrective action, retest image, evidence location:
Responsible reviewer and decision for the NEXT stage (not flight release):
```

Reproducible offline checks, which do not touch a board or serial port:

```powershell
pwsh -NoProfile -File Tests/host/run_tests.ps1
pwsh -NoProfile -File Tests/review/run_review_probes.ps1
pwsh -NoProfile -File Tests/services/run_service_tests.ps1
pwsh -NoProfile -File Tests/bringup/run_bringup_tests.ps1 -Python .\.venv\Scripts\python.exe
pwsh -NoProfile -File Tests/repository/check_repository.ps1
```

The [current review report](REVIEW_REPORT.md) records the three software review passes and their limits. Physical acceptance remains your next deliberate bench activity; no device was programmed or actuated while producing this system.
