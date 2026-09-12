# Atlas Ground Station and USB updates

Version 1.2.5 applies each selected servo angle directly, using the servo’s own position controller. The dashboard offers angle buttons and numeric entry across nominal ±50°. It retains the corrected PCB PWM numbering (right 1 to left 8), 16-sample ADC averaging and 8.55 V output-loop cutoff. See the [servo review](SERVO_FIX_REVIEW.md). Version 1.2.2 added the owner-selected 8.55 V ServoBench cutoff and repairs the march transition. It retains the 1.2.1 work that corrects the ADC reference scaling, exposes SW2 and SD diagnostics, adds startup audio, and provides a separate ServoBench image for manual KST X10 tests. Atlas uses battery power; USB-C carries data. Follow the board inspection and power requirements in [startup](startup.md), including the 16.3 V input limit and mandatory RGB inhibit. Keep J5 open and pyro loads disconnected. Servos stay disconnected until the [servo bench procedure](SERVO_BENCH.md) is satisfied.

## Start the dashboard

Double-click **Start Atlas Dashboard.cmd in your local clone**. It opens [Atlas Ground Station](http://127.0.0.1:8765). Keep the launcher running. **Atlas Dashboard Demo.cmd** opens explicitly simulated instruments and servo models with hardware actions disabled.

On a fresh Windows laptop, install Python 3.10 or newer with its Python launcher first. The Atlas launcher creates this clone's `.venv` and installs the bundled, SHA-256-verified pyserial wheel without network access. Do not copy another computer's `.venv`, browser shortcut, or absolute path. No Node packages, accounts, map service, or compiler are required for the dashboard. A failed setup leaves the error visible and details in `.atlas-launch.log`; an unusable environment is preserved under a timestamped name.

Launch paths are resolved from the batch file and Python source, regardless of the working directory. Repository documentation links are relative. `127.0.0.1` always refers to the computer running that copy of the dashboard; it does not connect to another laptop's server.

If the same clone already owns the port, the launcher opens its existing session. A different clone or older server on that port produces a clear message; close its launcher or run:

```powershell
& '.\Start Atlas Dashboard.cmd' --port 8766
```

For a setup/path check that does not open a server, browser, serial port or firmware operation:

```powershell
& '.\Start Atlas Dashboard.cmd' --check
& '.\Atlas Dashboard Demo.cmd' --check
```

The original [Tk dashboard](../tools/bringup/dashboard.py) remains available for ordinary Bringup diagnostics; the browser dashboard owns the ServoBench workflow.

## Connect and observe

1. Apply checked battery power and attach USB-C. Exit demo, choose **Connect device**, refresh ports, and select the STMicroelectronics Virtual COM Port. A COM number alone does not identify Atlas: the host validates the firmware profile, capabilities, UID and status frames.
2. **Overview** shows live attitude, histories, voltage, SW2 HIGH/LOW and SD detection/mount state. Switch LOW/HIGH describes PF12 electrically; no mechanical ON/OFF orientation is assumed.
3. **Start sensors** deliberately probes previously untested onboard sensors and GNSS once. BLE/radio and fixture tests are separate actions. An absent GNSS antenna means a valid navigation fix is not expected; an absent BLE module is not an installed sensor failure.
4. **Sensors** includes the ten ADC ranks and retained reference diagnostics. A healthy reference uses the H743's 16-bit factory and live values without the erroneous 12-bit conversion. Compare 3V3/PWM/5V/VIN against a meter; ADC readings are not a replacement for electrical qualification.
5. **Navigation** requires a valid fix for position and ground track. **Communications** shows USB, NINA BLE and RFD900x observations. J9's legacy “LoRa” label refers to a serial FHSS radio interface; RSSI, SNR and peer delivery are not fabricated.
6. **Test controls** contains explicit sensor, indicator, GPIO and media commands. **Servo workbench** contains the separate manual PWM procedure. RGB and pyro activation remain unavailable.
7. **Session log** records only after **Start recording**. Stop and export JSONL before closing the server. Records identify host UTC and live/demo source. The 12,000-record cap stops instead of overwriting old evidence.

Invalid or stale measurements are suppressed in instruments; retained observations are labelled. Histories hold 240 frames, about 120 seconds at 2 Hz. MCU status publication rate is not sensor sample rate. Model horn positions are commands/previews, never physical position feedback.

### If connection fails

Close an old server window before relaunching updated Python files; refreshing the browser alone does not replace the server. Select Atlas's USB COM device, not the PC's generic Communications Port. COM numbers can change between computers and after programming. ROM DFU has no application COM port.

The host opens with DTR low, waits 100 ms, clears old input, then raises DTR for a new handshake. This corrects the previous Windows startup race behind `Expecting value: line 1 column 1`. Missing handshakes time out clearly after eight seconds. Malformed telemetry still blocks hardware commands and retains an escaped, bounded rejected-record preview.

### SD card checks

Detection and mounting are separate. In **Test controls**, mount the card first, then read the prepared `ATLAS.TXT`. A missing read-test file is a filesystem error, not proof that the SD slot is broken. The dashboard displays named FatFs errors, controller failure stage, HAL status/error and detect-edge count.

The optional write/read comparison creates a 1,024-byte `ATLASCHK.TST` exclusively and checks its contents. It refuses to overwrite an existing file. It does not format the card. Unmount before removing media; power Atlas off before moving the card to a PC. Safely eject on the PC before returning it to Atlas. The September 11 card check mounted and read `ATLAS.TXT` successfully with the previous 1.1.1 firmware; no SD clock or bus-width change was needed for that result.

## Play the buzzer melody

Startup plays four rising notes once after the buzzer owner starts, even without a dashboard connection. This is a power-on sound, not a flight-readiness annunciation.

**Test controls → Indicators & logic → Imperial March** plays a single 42-note arrangement, 16.5 seconds. Version 1.2.2 restores the high-G repeat and short E–E♭–E turn in the bridge beginning at note 19, corrects the later B♭ landing, and places both opening phrases on four-second boundaries at 120 beats/minute. The closing phrase starts at 13.25 seconds with E♭–F♯–E♭–B♭ before returning to G; this is the owner's accepted Preview C. The owner's preferred lower octave is retained: 311–784 Hz, with a 300 Hz driver minimum. This remains an adaptation of the owner's supplied rough phrases. Timer reload and compares latch together before each tone starts. Firmware 1.1.1 reports its older 33-note/11.36-second sequence.

**Stop indicators** cancels the melody. USB/DTR loss, a changed session, fault or a new sensor/link command cancels it; there is no replay queue. Polling continues while it plays and late service skips elapsed notes. Firmware update is refused during audio. Status shows MCU playback progress; actual pitch, loudness and differential waveform still require acoustic/scope acceptance.

## Build and first installation

| Root launcher | Output | Capability |
|---|---|---|
| **Build Atlas Firmware.cmd** | `build/BenchMake/Atlas-Bringup.hex` and matching manifest | Diagnostics with PWM/pyro inhibited |
| **Build Atlas ServoBench.cmd** | `build/ServoBenchMake/Atlas-ServoBench.hex` and matching manifest | Diagnostics plus explicitly gated, one-channel manual servo PWM; pyro inhibited |

Builders need the Arm GNU compiler, CMake and GNU Make in addition to Python. They discover installed tool locations and report missing tools; they do not program hardware. Both emit ELF/HEX/BIN/manifest and validate target, vectors, hash and image agreement. The CMake Bringup/ServoBench presets are also available when using Ninja. Normal Debug/Release firmware is not accepted by this dashboard updater.

The launchers use Python directly, so PowerShell script-execution policy does not need to change. On a clone with no build artifacts, the dashboard still opens and connects; build a profile before verifying a new image. Select an explicit manifest for a custom build folder. Relative manifest paths resolve inside this clone.

An image older than 1.1.0 needs **one initial BOOT0/NRST DFU or SWD installation**. Follow [initial programming](startup.md#4-program-the-stm32-over-usb-dfu), verify the exact image, and cold-cycle power. An old application cannot implement a new command until it is replaced.

## Later updates without BOOT0/NRST

With Bringup/ServoBench software-DFU support installed, BOOT0 remains LOW:

1. Stop PWM and indicators, finish commands, and unmount SD. Keep loads isolated and battery/USB stable.
2. Build the desired profile. In **Firmware**, select **Bringup** or **ServoBench**, then **Verify image**. This freezes a validated bank-1-only image. An explicit manifest takes precedence over the selected profile; review the displayed profile and hash.
3. Choose **Update Atlas** and review the concrete image. The serial owner rechecks identity and idle state, then sends one UID-bound bootloader request. Firmware waits for acknowledgement transfer completion and uses a one-shot marker/software reset to enter factory ROM before HAL/RTOS setup.
4. The host requires a single DFU target, pins every programmer command to its DFU serial, and compares physical UID/family with the application handshake before writing. A wrong or ambiguous target aborts.
5. CubeProgrammer downloads and verifies the bounded HEX. The updater performs no mass erase, option-byte change or readout-unprotect. A failed verify does not run or retry the image automatically.
6. ROM 0x91/0x92 permits the application-start request. Other or unreadable ROM versions require a cold power cycle, which the UI reports explicitly. Even a supported ROM may need a cold restart: if the flash verifies but the application COM port does not return, disconnect both USB-C and battery, leave BOOT0 LOW, then restore battery power and USB-C. Reconnect and verify the new profile/version and live data. Do not repeat a verified flash just because the restart did not complete.

The September 11 test verified software-requested programming of 1.2.1 on this board. One ROM 0x92 update returned directly; the latest required the cold restart above. Automatic return is not yet reliably qualified.

A broken application, interrupted update or USB driver problem can still require physical BOOT0/NRST or SWD recovery. Factory ROM owns its own pins during DFU; application output inhibits do not govern ROM. See the same isolation requirements as initial programming. Normal/flight builds have no maintenance command parser and remove the dashboard's update route.

## Engineering details and references

The wire command is `ID bootloader UID0 UID1 UID2`; `software_dfu:true` advertises support. UID matching prevents accidental wrong-device writes; it is not authentication. BLE/radio cannot send these commands. Entry cancels on an expired drain deadline, dropped acknowledgement, session loss or newly unsafe/busy state.

An 8-byte `.atlas_boot` marker in DTCM requires a complementary value and software-reset cause. Cold boots never read uninitialized marker memory. The initial MSP is `0x2001FFE0`, reserving the ROM Go restriction's top 32 bytes; the existing stack/MPU guard remain. GNU and IAR layouts agree, but actual IAR compilation is unverified.

The server binds to loopback, serves a fixed asset allowlist, checks Host/Origin, requires a random session token, and bounds requests/captures. Opening the page never opens a COM port. Optional WebMCP telemetry is read-only.

- ST [AN2606](https://www.st.com/resource/en/application_note/an2606-introduction-to-system-memory-boot-mode-on-stm32-mcus-stmicroelectronics.pdf), H74xxx/75xxx bootloader resources and ROM-version restrictions.
- ST [CubeProgrammer command reference](https://dev.st.com/stm32cube-docs/prog/2.23.0/en/docs/markup/CubeProg_Command_Lines.html), target selection, read, download/verify and Go.
- [Servo bench procedure](SERVO_BENCH.md) and [review record](GROUND_STATION_REVIEW.md), including limitations of host tests and actual bench evidence.
