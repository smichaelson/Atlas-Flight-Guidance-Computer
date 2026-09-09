# Ground Station review record

## 2026-09-05 — buzzer melody, version 1.1.1

Added **Test controls → Indicators & logic → ♪ Imperial March**, using exactly the owner's supplied pitch sequence. The arrangement has 33 notes, selected octaves from 1,319–3,136 Hz, simple note/rest timing, and a requested total duration of 11,360 ms. No external score or additional tune phrases were imported. The original 1–10 kHz TIM15 driver and normal application API are unchanged.

Three review passes for this addition:

| Pass | Scope and evidence | Result |
|---|---|---|
| 1 | Firmware ownership, all 33 supplied notes, bounded elapsed-time scheduling, silence gaps, no replay/loop, stop/session/fault cancellation and DFU exclusion | Reviewed; the sensor owner services playback without delay loops or a new task |
| 2 | Actual C sequencer tests across tick wrap, every note/rest boundary, delayed-service skip, no restart while playing, complete stop, disconnect, epoch change, watchdog fault and partial timer-start failure; strict parser and Python client/API gates | Bring-up suite passes, including 14 existing Python cases and 36 Ground Station/updater cases; instrument and buzzer UI Node checks pass |
| 3 | Real browser preview, button placement and disabled demo behavior, developer logs; inert rendering checks for live readiness, older firmware, stale/blocked sessions, pending tests, active playback and available Stop | Passed; no browser errors and no hardware command sent by UI checks |

Debug and Release builds pass for both profiles. Bringup Debug is **213,928 flash bytes / 121,968 reported DTCM bytes**; Bringup Release is **112,616 / 121,880**. Both normal-image memory figures below are unchanged. The new Debug HEX passes offline verification with SHA-256 `65ea940b8e5f770c49952c48439c1ecb30b91f1a4fc1299d9c1c75575dfdb033`.

The `march` command is exact and argument-free. `hello.buzzer_melody=true` advertises support; validated `status.buzzer` reports playing state, note/count, actual driver frequency and last melody status. A command acknowledgement means scheduled, not acoustically verified. The laptop rejects unsupported firmware and repeated playback, and both laptop/MCU refuse software DFU while the melody is active. Stop or another sensor/link-owner operation cancels it. Delayed service skips overdue notes and cannot accumulate a catch-up backlog.

**Physical sound and timing remain untested; this change has not been flashed to the PCB.** Install Bringup 1.1.1 before using the new button. The existing power/load and hardware qualification requirements still apply. The older evidence below is retained as history, not the hash of this new package.

## 2026-09-04 — original version 1.1.0 review

Reviewed 2026-09-04. Three sequential author review passes were performed. These are distinct reviews with different scopes, not independent reviewers or a flight-safety approval. All automated tests below use host models or simulated/inert devices. **The physical board has not yet been programmed or tested for this change.**

The source and documentation survey covered startup and power constraints, hardware/module references, the original review evidence, both firmware profiles, linker/startup integration, USB/service ownership, the diagnostic protocol, host tools, and existing test harnesses. This repository is the project knowledge base; no separate application database was found.

## Review 1 — firmware entry and programming boundaries

Traced the command parser, actual console dispatcher, output/storage gates, USB completion counters, reset marker, startup hook, linker placement, factory-ROM vectors and programmer operations. Checked ST AN2606 revision 70 and the installed official CubeProgrammer 2.23.0 command help.

Corrections made during this pass:

- DFU requires matching 96-bit UID, a configured USB/DTR session, idle workers, unmounted SD and deasserted outputs. A queued reply alone is insufficient: the actual USB completed-byte counter must cover all queued bytes. Lost sessions, dropped data, state changes and the three-second deadline cancel entry.
- The laptop holds CDC open after acknowledgement. It deliberately establishes a fresh DTR boundary before its update handshake and never retries an uncertain request.
- The early hook consumes an eight-byte complementary marker only after an intentional software reset. Cold/brownout boot avoids reading uninitialized marker SRAM. ROM entry occurs before MPU/cache/HAL/watchdog/task initialization.
- GNU and IAR reserve the top 32 DTCM bytes for the documented H743 ROM Go-command stack restriction. Initial MSP is `0x2001FFE0`; the existing stack bottom and MPU guard remain `0x2001C000` and `0x2001BF00`.
- Every programmer connection is pinned to one enumerated DFU serial. Physical UID reads use **12 bytes**, then verify the H743-family ID and CDC identity before writing. ROM 0x90/unknown revisions and initial BOOT0 installation require a cold power cycle. Supported 0x91/0x92 software updates request application start only after verified download.
- Image verification freezes the artifacts and checks ELF/HEX/BIN hashes, profile, target, vectors and bank-1 address bounds. No mass erase, option-byte operation or unprotect is issued. Flash verification and application reconnection are separate results.

Disassembly confirms `main` calls `AtlasBoot_EarlyCheck` before `MPU_Config`; the GNU trampoline is `msr MSP,r0; cpsie i; bx r1`, without a stack-dependent epilogue. The marker is at `0x20000000`, initialized data begins at `0x20000008`, and BSS begins at `0x20000118`. IAR source membership, no-initialize section and assembly trampoline were checked statically; IAR was not compiled.

## Review 2 — regressions and adversarial failure cases

All existing host, focused review, service-model and bring-up suites pass. The bring-up run includes the original 14 Python dashboard/session/fixture tests, actual C console/JSON/output/storage tests, the Tk smoke test, new boot-marker tests and **33 new Ground Station/updater tests**. The protocol fuzz test processes 100,000 bytes. Node's instrument harness passes **16 checks** covering quaternion conversion, rail units, stale samples, GNSS validity and timestamp wrap.

New cases exercise wrong UID, multiple/missing-serial DFU devices, failed flash verification, old/unknown ROM revisions, first-install cold boot, exact programmer ordering, unchanged target serial, disconnected/demo command rejection, stale/malformed telemetry, partial serial writes, pending-operation locks, no automatic retry, explicit fixture/RF acknowledgements, recording bounds/source labels, and loopback API Host/Origin/token/body/path checks. Tests use mocked programmer/serial interfaces; they do not demonstrate Windows USB-driver interoperability.

Four Arm GNU 14.3.1 builds passed using CMake 3.26.4 and the documented MinGW Makefiles fallback:

| Profile | Build | Flash bytes | Reported DTCM bytes |
|---|---|---:|---:|
| Bringup | Debug, `build/BenchMake` | 212,744 | 121,904 |
| Bringup | Release, `build/BenchRelease` | 111,800 | 121,808 |
| Normal | Debug, `build/NormalDebug` | 205,332 | 89,960 |
| Normal | Release, `build/NormalRelease` | 106,060 | 89,936 |

The linker DTCM report includes the reserved stack allocation; these numbers are not measured task high-water marks. Each build also uses 64 bytes of AXI DMA RAM. Debug Bringup BSS ends at `0x20019C50`, below the guard. Ninja's compiler probe stalled in this environment; the Makefiles build is the executed evidence. Both Bringup image packages pass offline verification.

Reviewed Debug HEX SHA-256: `2bb6cebda7818cf411fbb1fa349760582c658adda3fb356e83d048dd65551c89`.

Reviewed Release HEX SHA-256: `fbf445b3c107137550b982269915f83d9b3021f3255678b51445735f22451599`.

## Review 3 — source-to-browser integration and operator workflow

Ran the real local Python server and browser application. Inspected all seven views and the actual rendered dashboard. Verified moving demo instruments, 14 sensor observations, ten ADC ranks, valid GNSS ground track, PPS/UART counters, communications limitations, disabled hardware controls in demo, firmware-package verification from the UI, recording start/stop and bounded export controls, explicit COM selection, and rejection of a connection request without a selected port. Disconnect clears values and histories. No hardware port was opened by these browser checks.

The responsive check at a 390-pixel requested viewport showed no document-wide horizontal overflow; navigation and wide tables retain their own scrolling. The main instrument layout was adjusted for laptop widths and secondary text made readable. Browser developer logs were empty before the intentional invalid-request test. The optional read-only WebMCP tool returned `source: demo`, freshness and the same subsystem observations shown on screen.

Installation review found Windows PowerShell's effective policy is Restricted. The final double-click launchers therefore call the existing Python environment directly and leave policy unchanged. The Python build helper was executed successfully; the launch helper's explicit dry run checks its resolved interpreter, manifest and arguments. Capture export derives its SIMULATED filename label from the saved records, so disconnecting cannot remove that label. The local export endpoint returned 23 records, each explicitly marked `source: demo`, retained as offline UI evidence.

Simulated data is labelled in the header, banner, footer and exported records. Invalid or stale main values are suppressed; retained raw observations are not promoted to current measurements. Quaternion mounting/calibration is unqualified. GNSS receiver response is not a position fix. RFD900x is a serial FHSS/SiK module, despite the connector's legacy LoRa label; RSSI, SNR and peer delivery are not supplied by the current firmware and are not invented.

## Physical acceptance still required

The owner specified battery power plus USB-C to the laptop for both dashboard and programming, with motors and pyro channels disconnected. The documented J5-open state, servo/load isolation, USB inspection and present 16.3 V input limit also apply. Factory ROM owns its own pins; application inhibits are not a guarantee about ROM pin states.

Remaining checks are: identify the actual CDC/DFU device and ROM revision; preserve initial flash where needed; install this image once with the documented factory-DFU procedure; cold boot with BOOT0 low; confirm version 1.1.0, inhibit flags and real telemetry; perform an explicit sensor sequence; capture GNSS/ADC phase diagnostics; complete one software-requested DFU update and observe application return. A missed acknowledgement or failed verification is a stop condition, not a reason for automatic retries.

The earlier GNSS and ADC3 physical findings remain open. RGB Q6–Q8 remains inhibited. Radio peer/RF tests, loaded outputs, GNSS position accuracy, axis mounting, real stack headroom, IAR parity, power-loss recovery and flight qualification are not established by these reviews. The automated update route is deliberately available in the inhibited **Bringup** profile; normal flight-application images do not expose this maintenance parser.

Use the [Ground Station guide](GROUND_STATION.md) for operation, [startup](startup.md) for initial programming/recovery, and [the existing review record](REVIEW_REPORT.md) for prior findings.
