# Remote telemetry 1.5.0 — review and installation record

Requested scope: two Atlas boards exchange read-only sensor/ADC/GNSS/health snapshots,
display the peer in its own dashboard tab, and report delivery/integrity statistics.
Three focused self-review passes are required; these are not independent certification.

## Review 1 — wire format and fault behavior

Completed code inspection and fault-injection tests for fixed packet bounds, CRC coverage,
two-shard erasure repair, fragment/noise resynchronization, sequence/tick wrap, duplicates,
reboots, stale assembly deadlines, finite send pacing and counter semantics. Review fixes:
late shards now expire the batch before acceptance; stale outgoing batches are discarded;
GF inverses are computed once per repair; whole-batch CRC failures count once per batch.
Existing command text has no dispatch path through remote packets. Passed offline.

## Review 2 — firmware integration and source separation

Completed inspection of sensor startup, saved stabilization/IO boundaries, UART ownership,
boot identity, console publication and local/remote validation. Sensors start before the
stabilization IMU is published; startup and individual UART writes retain finite deadlines.
Radio bytes have no control-dispatch path. Snapshot copies and USB records have bounded
storage; a rejected remote snapshot cannot refresh local freshness or complete a command.
The actual C formatter passes production Python decoding with signed, invalid and ADC
boundary values. Existing servo, autonomous stabilization, persistence, USB, storage,
GNSS/BNO and output-inhibit regressions pass. No actuator or SD-record format was changed.
Passed offline; real sensor timing and stack margins remain hardware checks below.

## Review 3 — release and dashboard

Completed final source/build/UI review. Normal Debug and Release, Bringup and ServoBench
builds pass. Host, service, focused review, bring-up, Python and browser model tests pass.
Repository link/header/build-membership checks and `git diff --check` pass. Review fixed
a misleading radio-test error that could overwrite a local broadcast-control failure;
the complete bring-up suite was rerun successfully. The new browser tab was also opened
against real 1.4.0 hardware: it correctly requests the upgrade, displays no fabricated
peer values, disables broadcast controls and produces no browser console errors.

Final ServoBench image: 251,232 flash bytes; DTCM 127,632 / 131,072 bytes (3,440 bytes
unallocated, including the existing statically allocated task stacks in the used figure).
HEX SHA-256: `ce070e207e6d49571dcd3617f343799b398633041b45ed462ec7280cd31f28dd`.
Both COM3 and COM4 identities were matched with fresh SW2 OFF, PWM/pyro/GPIO outputs OFF
and no supervisor faults before installation preparation. Three offline review passes
are complete; live 1.5.0 verification is still required below.

## Hardware installation and observations

Both boards have been programmed with the frozen image above and STM32CubeProgrammer
verified the flash bytes against that image. ROM UID reads matched the earlier USB UIDs:
COM3 `00330032-33335105-38393631`, COM4 `00340020-33335105-38393631`.
COM3's calibrated/reversed stabilization configuration was retained and its enabled flag
temporarily saved OFF for entry to DFU; it will be restored after startup checks.

After the requested full battery/USB power cycle, both boards reported 1.5.0 with the
expected identities, independent nonzero RNG boot IDs and automatic sensor startup.
ADXL, LSM, MMC, barometer, BNO and GNSS initialization all succeeded. GNSS NAV messages
advance with no reported fix; coordinates remain hidden. ADC scans and all sensor report
counts advance. BLE remains available as an explicit probe.

During a 35-second bidirectional measurement each gateway received **350/350 packets and
35/35 snapshots**, with no new CRC/header/UART errors, missing packets, lost batches or TX
errors. USB published 50/51 remote records during that interval. Maximum observed LSM
sample ages were 62 ms / 56 ms, below the unchanged 100 ms cutoff; these are sampled
observations, not a proof of every scheduling interval. Minimum task-stack watermarks
were `[1721,1678,1361,1328,943]` and `[1721,1678,1361,1683,943]` words. No supervisor faults
or enabled PWM/pyro/GPIO outputs were observed.

Startup totals already contained one CRC-rejected packet in each direction. COM3 had six
missing shards and one lost snapshot; COM4 had one missing shard successfully repaired
by parity. These counters remain visible and were not reset to hide errors. Each board
also had one recorded MMC transaction error; subsequent magnetic readings were current.

Three explicit round-trip tests per direction all succeeded during streaming (197–415
ms). COM3's original calibrated/reversed configuration and saved enabled setting were
restored with a 64-byte SD readback-verified save; SW2 remained OFF and PWM inactive.
The local dashboard reconnects normally and periodic acknowledged monitoring is restored.

A deliberate COM4 broadcast pause produced `available=0`, zero useful throughput and
**Remote data stale**, with all remote sensor/ADC values hidden in the actual browser.
COM3's local status stayed fresh and fault-free. Resuming the broadcast automatically
restored complete validated snapshots and current measurements.

For the final standalone test the user removed COM4 USB, disconnected its battery for
five seconds, and reconnected battery alone with BOOT0 LOW and SW2 OFF. Windows listed
only COM1 and COM3. COM3 recognized COM4's changed boot ID (`D7D6C574-6BF39EBC` to
`A6DB4E0E-D0B90BC9`), retained the correct UID and automatically received fresh sensor,
ADC and GNSS records without a command to the peer. Across a 30-second observation,
30 distinct sequences were visible, with 29 new completed snapshots / 290 packets
between the first and final capture (29.408 seconds). No new packet/CRC/header/UART/TX
errors or lost snapshots occurred. Measured mean throughput was 986.1 packet bytes/s
and 504.9 useful payload bytes/s. Existing periodic ping monitoring also remained active.

The actual **Remote Atlas** browser view was inspected with the battery-only peer,
including sensor rows, ten ADC channels, no-fix GPS behavior, board health and delivery
statistics. Final state: COM3 owns the dashboard USB connection, COM4 is on battery alone,
both broadcast automatically, COM3's saved stabilization enable is restored and all
outputs remain inactive with SW2 OFF. No physical servo/pyro actuation was performed
for this telemetry update. Three review passes and the requested live verification are
complete; range, deliberate RF interference and a valid outdoor GNSS fix were not tested.

Evidence files are retained locally under `build/telemetry-150-*`; sensor/location logs
are not added to Git. A successful programmer start request alone was not treated as
proof that the application started.
