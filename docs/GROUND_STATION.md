# Atlas Ground Station and USB updates

Version 1.1.0 adds a local browser dashboard and a guarded request to enter the STM32H743 factory USB bootloader. The board runs from its battery; USB-C carries data to the laptop. Keep motors, servos and pyro loads disconnected and J5 open. The existing board, power and USB inspection requirements in [startup](startup.md) still apply, including the present 16.3 V input limit and mandatory RGB inhibit.

## Start the dashboard

Double-click **Start Atlas Dashboard.cmd** in the repository. It uses the project's existing Python virtual environment and opens `http://127.0.0.1:8765`. Keep the launcher running while using the dashboard. **Atlas Dashboard Demo.cmd** opens moving, explicitly simulated instruments with serial access disabled.

If a dashboard is already running, either launcher opens that existing session. Use **Explore demo** from its disconnected state to enter simulation.

The same launch can be run from the repository root:

```powershell
.\.venv\Scripts\python.exe tools/bringup/launch_ground_station.py
# Offline demonstration:
.\.venv\Scripts\python.exe tools/bringup/launch_ground_station.py --demo
```

If creating a fresh checkout, install Python 3.10+ and the existing pinned requirement first:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/bringup/requirements.txt
```

No Node packages, internet connection, accounts, hosted service or external map tiles are needed to use the dashboard. The original [Tk dashboard](../tools/bringup/dashboard.py) remains available.

## Connect and observe

1. Start with checked battery power and the documented safe physical setup, then attach USB-C.
2. Exit demo if running. Choose **Connect device**, refresh ports, explicitly select Atlas's COM port and confirm the bench setup. A COM port is not proof of the correct firmware: the application waits for the inhibited Bringup handshake and valid status frames.
3. Choose **Start sensors** to probe each previously untested onboard sensor, including GNSS, once. BLE/radio and fixture tests are separate deliberate actions. There is no automatic sensor probe on connection and no automatic retry.
4. Use **Overview** for attitude, motion/pressure histories, rail input, GNSS altitude and subsystem health. **Sensors** shows all 14 existing observation rows, ten ADC ranks and failure details. **Navigation** shows valid 3D fixes, local ground track, accuracy, NAV/UART counters and PPS. The instrument orientation follows the supplied BNO quaternion; body-axis mounting/calibration remains unqualified.
5. **Communications** displays USB, NINA-B112 BLE and RFD900x observations. The legacy J9 “LoRa” connector is a serial FHSS radio interface. Firmware does not expose RSSI/SNR or prove peer delivery, so these are never fabricated.
6. **Test controls** contains the existing allowlisted probes, read-only media operations and explicitly confirmed indicator/fixture/link tests. RGB, PWM and pyro enable/fire controls remain absent. A pending or uncertain command blocks new work.
7. **Session log** records only after **Start recording**. Stop and export JSONL to keep evidence. Every frame includes its host UTC and `source` (`live` or `demo`). The 12,000-record cap stops recording before overwriting old evidence. Export and explicitly clear the previous capture before recording again. Closing the server discards unsaved in-memory evidence.

Numbers with invalid/stale sample state are suppressed in the main instruments. Retained sensor-table values are labelled as last observations when the connection is stale. GNSS coordinates require a valid fix; a responding receiver alone is insufficient. Histories are bounded to 240 status frames (about 120 seconds at the firmware's 2 Hz publication rate). Status cadence is not the underlying sensor sampling rate.

## Play the buzzer melody

With **Bringup 1.1.1 or later** installed and a confirmed live connection, open **Test controls → Indicators & logic → ♪ Imperial March**. The 33-note arrangement uses the owner's supplied pitch sequence, with simple timing and selected octaves inside the existing 1–10 kHz driver limits. It lasts about 11.36 seconds and plays once. The dashboard displays the current note or rest from MCU telemetry.

**Stop indicators** cancels it. USB/DTR loss, a changed session or a watchdog fault also cancels at the next owner service. Another sensor/link owner command stops it before starting that operation. Sensor polling continues during playback; late service skips elapsed notes instead of replaying them. Firmware-update entry is refused while the melody is active. Existing firmware and demo mode keep the button disabled. Driver success does not verify actual sound; acoustic/timing testing on the PCB is still pending.

## Build and first installation

Double-click **Build Atlas Firmware.cmd**, or run:

```powershell
.\.venv\Scripts\python.exe tools/bringup/build_firmware.py
```

The Windows helper uses the installed Arm GNU compiler, CMake and GNU Make. It produces and verifies `build/BenchMake/Atlas-Bringup.elf`, `.hex`, `.bin` and `.manifest.json`; it never opens a serial port or programs hardware. The launch helper selects that manifest when present. The existing Ninja Bringup presets remain supported separately; select their manifest explicitly if using another build directory.

The double-click launchers use Python directly, so Windows PowerShell's script-execution policy does not need to change. The `.ps1` helpers remain optional for environments where those scripts are permitted.

An older image does not contain the new request handler. **One initial installation via BOOT0/NRST and factory DFU, or via SWD, is required.** Follow [the original initial-programming procedure](startup.md#4-program-the-stm32-over-usb-dfu), select the newly verified 1.1.0 image, preserve existing flash if needed, and retain verification evidence. Use a cold power cycle for this first installation. No software can make an already running older image respond to a command it does not implement.

## Later updates without BOOT0/NRST

With Bringup 1.1.0 running, BOOT0 left in its normal LOW state, and battery/USB-C connected:

1. Finish all tests and unmount the card. All GPIO pulses must have expired. Keep physical loads isolated.
2. Build the new Bringup image. In **Firmware**, choose **Verify image**. An explicit manifest path can select a different build. The tool validates hashes, target, profile, vectors and the complete HEX address range, and stages a frozen copy.
3. Choose **Update Atlas** and review the specific image/update action. The serial owner performs a fresh handshake and sends one UID-bound `bootloader` command. Firmware requires idle services, deasserted outputs and unmounted media. It waits for the acknowledgement's USB transfer completion, then uses a one-shot RAM marker and software reset to enter factory ROM before HAL/MPU/cache/RTOS setup.
4. The laptop waits for a single DFU device. Every programmer connection is pinned to that device's DFU serial. It reads the physical 96-bit UID and chip-family ID and compares them with the CDC handshake before any write. A wrong or ambiguous target aborts.
5. CubeProgrammer downloads the bank-1-only HEX with verification. No mass erase, option-byte operation, readout-unprotect or arbitrary memory-write command is issued. If verification fails, it does not start the application or automatically retry.
6. For ROM 0x91/0x92, the updater requests application start. Other or unreadable ROM versions finish with an explicit **battery power cycle required** result, preserving the older-ROM power-configuration restriction. Reconnect to verify the new firmware identity and streaming; flash verification alone is not proof that the application restarted.

Keep battery power and USB stable during the write. A broken application, interrupted update, unavailable USB driver or unsupported ROM behavior can require physical BOOT0/NRST or SWD recovery. Factory ROM owns its own pin configuration; application GPIO/LED inhibits do not describe the ROM interval. Keep the same physical isolation used for initial DFU programming.

The supported automated workflow programs the **Bringup profile**. Normal Debug/Release builds retain the early reset hook, but expose no remote maintenance command parser; switching to an arbitrary normal/flight image removes this dashboard's update route. A flight maintenance interface needs a separate operational safety design.

## Engineering details and references

The command is `ID bootloader UID0 UID1 UID2`, with three decimal 32-bit words from the current handshake. `hello.software_dfu=true` advertises support. Identity is an accidental-wrong-device guard, not authentication against a malicious USB host. Requests cannot arrive through BLE/radio. A 3-second USB drain deadline, dropped acknowledgement, lost session or newly busy/unsafe state cancels entry. Commands received while entry is pending are rejected.

The retained 8-byte `.atlas_boot` section is outside C initialization, in DTCM. A complementary marker and software-reset cause are required; cold/brownout boots never read uninitialized marker memory. The marker is cleared before attempting ROM entry. The main stack starts at the existing `0x2001C000`, with initial MSP `0x2001FFE0`; reserving the top 32 bytes addresses ST's documented ROM Go limit while preserving the `0x2001BF00` MPU guard. GNU and IAR layouts agree; actual IAR compilation remains unverified.

The local server binds only to `127.0.0.1`, serves a fixed asset allowlist, checks Host/Origin, requires an unpredictable session token for API access, rejects cross-site requests and bounds request/capture/history sizes. Opening the page does not open COM ports. Its optional WebMCP tool only reads telemetry; it cannot issue hardware commands.

- ST [AN2606, revision 70, H74xxx/75xxx tables 135–136](https://www.st.com/resource/en/application_note/an2606-introduction-to-system-memory-boot-mode-on-stm32-mcus-stmicroelectronics.pdf): ROM vectors, resources and version-specific restrictions.
- ST [CubeProgrammer command reference](https://dev.st.com/stm32cube-docs/prog/2.23.0/en/docs/markup/CubeProg_Command_Lines.html): serial selection, memory reads, download/verify and application start; checked against the installed 2.23.0 CLI help.
- [Ground Station review record](GROUND_STATION_REVIEW.md): three review passes, executable evidence and hardware acceptance status.
