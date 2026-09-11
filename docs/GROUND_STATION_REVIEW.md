# Ground Station review record

**Current correction:** [ServoBench 1.2.4 review](SERVO_FIX_REVIEW.md). The 1.2.2 voltage tests below missed a second cutoff in the full output loop. That defect was reproduced and corrected in 1.2.3; earlier acceptance statements do not establish sustained servo output.

## 2026-09-11 — 1.2.2 voltage policy and melody follow-up

The owner requested an 8.55 V ceiling and explicitly confirmed changing **voltage gates only**, keeping Stop, USB-disconnect shutdown, timeouts and pulse/travel limits. This supersedes the 1.2.1 ServoBench voltage limits below. KST still rates X10 V8.0 at 4.8–8.4 V; the selected cutoff is an owner setting above that rating, not a revised component specification.

These are three sequential author review passes, not three independent reviewers.

| Review pass | Scope and evidence | Outcome |
|---|---|---|
| 1 — firmware | Real IO-owner tests of 8550/8551 mV, zero, invalid/stale PWM, reference/monitor faults, unrelated ADC ranks, eight channels, USB/stop fences, pulse bounds and 3 s / 30 s limits; normal service tests | New voltage policy passes. No separate low-PWM, 3V3-band or arm-feed gate in ServoBench. Normal PWM/pyro policy remains unchanged |
| 2 — host and melody | 43 Ground Station/updater cases, 18 ServoBench cases, production console/schema interop; all 42 note/rest boundaries across tick wrap, irregular 2–17 ms service and delayed-service skip | Host/UI no longer require meter entry/agreement. Firmware advertises `servo_pwm_max_mv=8550`; old firmware remains stoppable but must update before enabling. Bridge regression asserts the high G, short E–E♭–E turn and later B♭ landing; closing-phrase regression includes F♯ at 13.5 seconds. Opening phrases are 4000 ms each; total 16500 ms |
| 3 — build, browser and installation | Both final Debug profiles built and checked; actual browser old/new-firmware enable gates and supply-monitor layout; complete-file hash checks with source/image backups; UID-bound software DFU and live telemetry/audio acceptance | ServoBench 1.2.2 reports 8550 mV, 42 notes and 16500 ms. The owner accepted the ending on the PCB. The old version remained blocked even with setup checked, while Stop stayed available; the new version allowed Enable at 8.408 V after setup confirmation. Enable was never clicked, and confirmation was cleared |

The melody retains the owner's accepted octave. Note 19 still begins the bridge; the missing high-G return is inserted as note 22 before the descent. The descending E/E♭ steps now last 125 ms each and return to E before A♭. The phrase-boundary fix changes the two earlier final-G tone/rest allocations. After the owner accepted Preview B's descent but identified an error near 13 seconds, Preview C changed only the closing phrase to E♭–F♯–E♭–B♭–G–E♭–B♭–G, starting at 13.25 seconds. The owner explicitly accepted Preview C. This is an adaptation of the supplied rough melody, not a claim of a complete published score. The laptop WAV is synthesized from the firmware table; it does not model PCB acoustics.

No physical servo motion is authorized by merely changing this setting or opening the workbench. Outputs remain off until explicit channel enable. The ADC cutoff uses individual measured samples without filtering/hysteresis: any sample above 8550 mV stops PWM. A usable nonzero PWM reading is required; other ADC ranks remain visible as diagnostics. ADC reference and output-monitor faults still shut down output. PWM-off does not remove servo power.

### Final 1.2.2 build and live evidence

- Final ServoBench HEX SHA-256: `26322a00291ef7d2f3c55a9b47a6a7b6118ad3409d7e09d5cc850091c7c04e04`; binary 218136 bytes. Ordinary Bringup: `fca33a6bfe07727610b1a00da535ee77aaf9a61b163db83e01d35b4ed12f4eda`; binary 215984 bytes. Both target STM32H743ZIT6 at flash base `0x08000000`.
- The final firmware table matches every note/tone/gap in accepted Preview C; independently resynthesized PCM matches the audition WAV exactly. The WAV SHA-256 is `ceb544fb1d783fd60f768f3580970d1ba61e9026d445f5ad0a310684776e2df1`.
- Software DFU matched UID `00330032 33335105 38393631` / DFU serial `200364500000` and verified the write. ROM Go returned success but USB did not return. The owner cold-cycled both battery and USB with BOOT0 LOW; COM3 then identified ServoBench **1.2.2** on the same UID. The original updater result is retained: `application_start_requested=true` does not prove application reconnection. Reliable warm restart remains unresolved.
- The final recording contains **188 live records / 180 status frames over 89.5 seconds**. All ten ADC ranks remain valid, ADC status is OK, and sample counts advance 12657→30557. Mean 3V3 is 3.3428 V; mean PWM is 8.4203 V, with sampled range **8.302–8.566 V**. The one captured PWM sample above the requested ceiling correctly reports `servo.ready=0` at 8.566 V. This verifies the readiness gate with outputs already off, not a physical PWM cutoff waveform.
- All captured GPIO-output, PWM and armed masks are zero. There are no decoder errors or supervisor faults. Sensors and GNSS telemetry continue during the 33 captured playing statuses; 500 ms telemetry cannot observe every short note. Playback reaches note 42 and returns idle, and the owner answered **“Yes, it sounds right”** to the board audition. MMC retains one initial sample timeout before valid data, as in the previous acceptance. GNSS has no fix with the antenna absent; BLE/radio were not exercised. SD is detected and left unmounted after restart; this revision makes no new SD changes.

The separate 1.2.2 portable-package verification record checks the actual extracted archive, all file hashes, both image manifests, all four Windows launchers from an unrelated working directory, a newly created local Python environment and repository links. No copied environment or owner-specific launch path is required. The other physical laptop and real servo motion remain untested. The changes are local and have not been committed or pushed to Git.

## 2026-09-11 — ADC, SD, SW2, ServoBench, audio and portable launch

The diagnostic release is **1.2.1**. It corrects the H743 reference calculation, exposes SW2/SD state and retained SD errors, adds an isolated one-channel servo profile, plays a startup chime, lowers the revised march by an octave after the owner's first audition, and bootstraps a fresh Windows clone without copied environment paths. Pyro/RGB and the flight-control hook remain inhibited in both bench profiles.

Three sequential **author review passes** cover the implementation. These are distinct passes over the work, not claims of three independent reviewers.

| Pass | Inspection and executable evidence | Findings resolved / boundary |
|---|---|---|
| 1 — firmware and electrical contracts | Schematic PD3 SD detect / PF12 SW2 / eight PWM pin mappings; exact KST X10 V8.0 specification; real H743 factory/live VREF widths; output owner, register deassertion, USB/cancellation epochs, stale ADC, queue age, timer bounds, rollover, SD controller resets and audio preload | Removed the erroneous live-16-to-12-bit reference conversion; replaced the misleading correct-value HAL mock with tests of the actual helper. Added SD failure retention assertions across controller reset/remount. Exercised all eight servo mappings, 3 s idle / 30 s hard session limits and stop-before-queued-enable. Buzzer ARR/CCR latch together before output. Normal and ordinary Bringup paths remain inhibited as before |
| 2 — host and browser controls | Production C framing/console interop with Python, 43 Ground Station/API/updater tests, 14 servo host cases, parser fuzz, malformed capabilities, stale/blocked sessions, explicit DMM/channel approval, no generic-command bypass, stopped USB handle cleanup, keyboard model interaction and real browser layout at 390 and 1280 pixels | Fixed disconnected-port DTR errors leaving host state uncleared, a checkbox causing horizontal overflow, navigation scrolling and stale melody metadata. A final wide-screen check found a full-width link squeezing the SW2/SD labels; its flex width is now scoped to that strip. Demo model changes never enabled hardware. Stop remains available despite stale/blocked/pending work; the MCU cutoff does not rely on delivery of browser cleanup |
| 3 — packaging, reproducibility and real board | Both diagnostic Debug images plus normal Release build; host/service/review/repository checks; clean-clone Windows launch from unrelated CWD in a Unicode/space/parenthesis/ampersand/literal-percent path, with no copied venv; real UID-bound software DFU, pre-write flash backup, verified flash, live ADC/media/sensor/audio capture | Fresh-clone setup uses the bundled hash-pinned wheel. Corrected the test runner's local-Python selection and actual launcher filename references. Real DFU exposed a Windows long-read ACK-loss race and CubeProgrammer 2.23's `Device Index` listing; both were corrected and covered by regression cases, including missing/corrupt ACK refusal. Final 1.2.1 update/audition evidence is recorded below |

### Source and build checks

- `Tests/review/run_review_probes.ps1`: production reference/divider helper, timing at three RTOS tick rates, GNSS transport, real BSP/disk/FatFs over a RAM card, retained first SD HAL error and explicit-remount clearing.
- `Tests/services/run_service_tests.ps1`: normal IO/pyro policy, USB, storage ownership, expansion and no dynamic C heap. `Tests/bringup/run_bringup_tests.ps1`: strict protocol fuzz, both bench IO profiles, both console profiles, Python schema interop, hidden Tk and browser control gates. `Tests/host/run_tests.ps1`: drivers including 311 Hz buzzer and CEVA integration. These tests are inert; no hardware ports are opened.
- `Tests/bringup/test_portability.py`: real double-click launcher paths for dashboard/demo (`--check`) and both builders (`--help`), no server/COM/flash; verifies a clone-local interpreter and pyserial 3.5. This is a fresh-clone simulation on the development Windows PC, not testing the owner's other physical laptop.
- Repository local-link/heading/Doxygen/artifact checks and JavaScript syntax checks pass. No cloud host or fixed contributor path is needed at runtime. Builders need locally installed Arm GNU/CMake/Make; dashboard use needs Python 3.10+ only.

| Final artifact | HEX SHA-256 | Binary bytes |
|---|---|---|
| `build/BenchMake/Atlas-Bringup.hex` | `1e67f24e64c80c9e4dffa5107806983f334667cedb2b84e1d62b0437a3dfe2b3` | 215956 |
| `build/ServoBenchMake/Atlas-ServoBench.hex` | `a513022acf569d437fde2e4bb7d09e4be32beea4b3deef0deca697f5b57f66db` | 218120 |

Both diagnostic images validate as STM32H743ZIT6, bank 1 only, MSP `0x2001FFE0`, and matching ELF/HEX/BIN. ServoBench DTCM use is 122184 / 131072 bytes, including reserved/static regions; actual stack watermarks are captured separately. IAR compilation is not claimed.

### Physical evidence and limits

The owner confirmed battery/USB-C power, no connected servos, unrestricted future horns, J5 open, pyro loads disconnected, absent GNSS antenna and absent BLE module. DMM values were 3V3 **3.36 V** and PWM **8.42 V**. After the ADC fix, the owner also confirmed the approximately 16.37 V input reading and stated the components support 17 V. Unloaded tests continued under that owner-stated allowance; this review does not independently qualify 17 V or erase the schematic's 16.3 V note. The KST supply limit is a separate constraint.

- **SD:** the owner's FAT32 card was first inspected on drive E: without formatting or overwriting data. A new 23-byte `ATLAS.TXT` was prepared. After a safe powered-off move into Atlas, old 1.1.1 already mounted/read it successfully. On 1.2.0, mount and 23-byte read again passed, then the exclusive `ATLASCHK.TST` write/read comparison verified **1024 bytes**, FatFs 0, controller/HAL 0, no storage errors. This proves the slot and card operate in that setup; it does not identify an unobserved prior intermittent fault. SD timing and bus width were not changed. An existing `ATLASCHK.TST` is preserved, not overwritten on a later test.
- **SW2:** a 101-frame, approximately 50-second live capture while the owner held both positions included both electrical states. PF12 configuration already matched the schematic. The dashboard now shows HIGH/LOW and a fresh GPIO timestamp; no mechanical ON/OFF orientation is assumed.
- **ADC / USB:** the first new firmware capture contains 197 live records, 186 status frames over 92.5 s. Every ADC frame is valid (`valid=1023`, status 0); counts advance 29906→48406. Mean 3V3 **3.3552 V**, PWM **8.4252 V**. Raw sample variation is retained, not hidden: 3V3 3.316–3.390 V, PWM 8.302–8.518 V. No decoder errors, supervisor faults, reference/HAL faults or output activations occurred. Readings still require divider calibration and noise/ripple qualification.
- **Sensors / GNSS:** all six requested probes completed OK. Motion/barometer and all four BNO streams advanced; BNO transport/protocol errors were zero. MMC recorded one initial measurement timeout before its first sample, then resumed valid samples without further errors in the capture; this is retained rather than counted as a completely error-free sensor test. GNSS identity/configuration succeeded and 722 NAV frames arrived, no CRC/UART/drop errors. Fix=0 and SV=0 are expected with the antenna absent. BLE/radio were not probed or transmitted.
- **Audio:** the owner heard the four-note startup chime. The initial revised 39-note/15.5 s march completed in MCU telemetry while ADC/sensor polling continued; the owner found it recognizable but requested pitch adjustment. Version 1.2.1 lowers only that melody by one octave (311–784 Hz) and leaves startup unchanged. Scope verification of frequency, differential drive and audio stop latency is still separate from audible confirmation.
- **Servo:** all physical PWM outputs remained off. Models, host gates and MCU tests were exercised; no physical KST movement is claimed. The measured 8.42 V exceeds KST's 8.4 V operating maximum. The [workbench](SERVO_BENCH.md) requires measured and ADC PWM in 4.8–8.3 V with checked ripple/overshoot before connection, one channel, fixed 1520 µs neutral and conservative default travel.
- **Update:** old application 1.1.1 entered ROM by software request; the initial host stopped before writing when the short acknowledgement was lost. Independent ROM enumeration/UID read identified the same physical board, serial `200364500000`, ROM **0x92**, before a new 1 MiB bank-1 backup was saved. The 1.2.0 HEX was programmed/verified and restarted using supported ROM Go; COM3 reidentified the same UID and new profile. The corrected host reader drains short ACKs immediately and still refuses missing/corrupt acknowledgement; target selection remains serial/UID/family-bound.

### Final 1.2.1 acceptance

All three author review passes are complete. The installed Desktop dashboard performed the 1.2.0 to 1.2.1 update: application acknowledgement, software DFU entry, physical UID match and verified programming all passed. ROM 0x92 accepted the application-start command, but this time the application COM port did not return. No repeat flash was attempted. The owner removed both battery and USB-C power, left BOOT0 LOW, and restored power; COM3 then identified **ServoBench 1.2.1** with the same UID. The owner confirmed the startup chime and approved the march one octave lower.

The final capture contains **332 live records / 322 status frames over 160.5 seconds**. Every ADC frame is valid; the sample count advances 9582 to 41682. Mean 3V3 is **3.353 V** and PWM **8.4294 V**, with the unfiltered sample variation retained in the capture. All PWM, general-output and armed masks remain zero; decoder errors and supervisor faults remain zero. SD mount and the 23-byte read pass again with FatFs/HAL/storage errors zero. Motion, pressure and all BNO streams advance; GNSS receives 764 frames without CRC/UART/drop errors and has no fix with the antenna absent. MMC retains one initial timeout, then produces valid samples. The 39-note march completes and returns to idle.

The final live browser check rotated channel 7's preview to 1720 microseconds by keyboard. Physical PWM remained off and Enable remained disabled at the measured supply voltage. Real servo motion and waveform qualification remain pending a suitable supply and the workbench procedure.

Software-requested flashing without BOOT0/NRST is demonstrated. **Reliable warm application return is not yet qualified:** one ROM 0x92 update returned directly and the latest required a cold restart. After a verified flash, if the application COM port does not return, disconnect both USB-C and battery, keep BOOT0 LOW, restore battery power and USB-C, and reconnect to verify identity/version. This recovery does not require another flash.

The exported bench-evidence package retains both live captures and summaries, the complete final programmer result (including its original `application_reconnected: false`), test/build logs, and the pre-write 1 MiB bank-1 backup with identity/hash metadata. Later reconnect evidence is separate; historical results are not rewritten. The raw device backup is not byte-identical to the previous on-disk 1.1.1 binary, so the raw backup and previous source/artifact backup are preserved separately.

Primary references: [KST X10 V8.0 exact specification](https://cdn.shopifycdn.net/s/files/1/0570/1766/3541/files/X10_V8.0_Technical_Specifcation_ee6acc4d-79de-4d61-b686-3288dc5d4153.pdf?v=1704951839), ST [H743 datasheet](https://www.st.com/resource/en/datasheet/stm32h743zi.pdf), [upstream LL ADC header](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/refs/heads/master/Inc/stm32h7xx_ll_adc.h), [AN2606](https://www.st.com/resource/en/application_note/an2606-introduction-to-system-memory-boot-mode-on-stm32-mcus-stmicroelectronics.pdf), and [CubeProgrammer 2.23 command reference](https://dev.st.com/stm32cube-docs/prog/2.23.0/en/docs/markup/CubeProg_Command_Lines.html). The installed CLI help was also inspected; `-r32` and upload sizes are bytes.



## 2026-09-08 — Windows dashboard connection correction

The owner installed the existing Bringup 1.1.1 HEX and reported an immediate JSON error on COM3. The browser dashboard opened with DTR already high. The installed Windows pyserial implementation configures DTR before its `PurgeComm` call, allowing the board's first handshake packet to be discarded while the remaining bytes arrive. The updater already used a delayed, DTR-low opening sequence; the browser dashboard did not.

The correction opens with DTR low, waits 100 ms for the previous console session to settle, purges old input, and then raises DTR. Setup failures close the temporary handle without publishing a live connection. An eight-second startup deadline explains missing/incomplete handshakes, and rejected records now include up to 96 escaped input bytes in the error log. Corruption, stale data, missing inhibits and uncertain commands continue to block hardware actions; no automatic command or retry was added.

Three sequential author review passes were performed:

| Pass | Scope and evidence | Result |
|---|---|---|
| 1 | Traced actual installed Windows `Serial.open`, firmware DTR-triggered `hello`, dashboard opening order and existing updater; an inert Windows packet/purge model failed before the fix and passed after it | Confirmed the startup race and corrected the ordering without changing firmware |
| 2 | 42 Ground Station/API/updater cases, including cleanup failure, missing/partial handshake timeout, no command writes during connection, corrupted input followed by valid telemetry, bounded escaped errors and existing update restrictions | Passed; malformed telemetry cannot silently recover into command readiness |
| 3 | Physical USB-C connection after the owner reconnected Atlas with the documented isolated-load setup | COM3 identified Bringup 1.1.1, then delivered 24 validated status frames over 12 seconds with zero decoder errors; no test or programming command was sent |

The physical capture reports supervisor fault 0, parser errors 0, response drops 0, USB drops/timeouts 0, and PWM/armed/output masks 0. It also reports **ADC reference failure stage 8 (computed VDDA range)**, `power.status=6`, and zero valid analog samples. This is an unresolved measurement fault, separate from the repaired USB handshake. No sensor probes, buzzer playback, software DFU cycle or flash write were performed during this connection diagnosis.

The on-disk Bringup Debug HEX still verifies as SHA-256 `65ea940b8e5f770c49952c48439c1ecb30b91f1a4fc1299d9c1c75575dfdb033`. No firmware or image bytes were changed by this correction. Earlier statements below that hardware had not yet been flashed describe the dates of those earlier reviews.

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
