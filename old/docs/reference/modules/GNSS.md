# NEO-M9N GNSS and PPS

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasGnss_Init()` | Flushes stale USART receive/error state, starts interrupt reception, proves bidirectional UBX communication with `MON-VER`, and only then starts the shared 1 MHz PPS capture. |
| `AtlasGnss_ConfigureRam()` | Applies measurement/message settings to volatile RAM, requires a correlated UBX ACK, polls those keys with `CFG-VALGET`, and accepts success only when every value matches. |
| `AtlasGnss_Service()` | Consumes a bounded byte budget through a checksum-validating UBX state machine and decodes NAV-PVT. |
| `AtlasGnss_SendUbx()` | Builds UBX framing/checksum and optionally waits for ACK/NAK matching the transmitted class/ID. |
| `AtlasGnss_GetPps()` | Copies an interrupt-owned PPS snapshot coherently so foreground code cannot observe fields from different pulses. |
| `AtlasGnss_GetLatestNavPvt()` | Copies the most recent complete 92-byte NAV-PVT solution. |
| `AtlasGnss_OnPpsCapture()` | Records a TIM2 capture and wrap-safe pulse period in ISR context. |

Source: [`atlas_gnss.h`](../../../App/Inc/atlas_gnss.h) and [`atlas_gnss.c`](../../../App/Src/atlas_gnss.c).

## Validation state

Host tests cover selected NAV-PVT fields, checksum rejection, PPS wrap arithmetic, and RAM-configuration ACK/readback. The readback test returns keys out of request order and rejects a wrong value. Additional review probes exercise a receiver that transmitted before interrupt RX was armed, one bounded abort/flush/arm retry, an identity failure followed by a deliberate successful retry, the shared running TIM2 handle, fragmented/truncated frames, and UART overflow/restart recovery. These are software checks, not complete physical startup acceptance.

The 2026-09-04 board log reports GNSS init status `ATLAS_ERROR_IO` before any version, frame, or command-timeout evidence. Every later button press returned `ATLAS_ERROR_STATE` because version 1.0.1 prohibited a second attempt. That record cannot identify whether the original I/O error occurred while arming USART1 or starting PPS. It is consistent with an STM32 receive-to-idle startup abort caused by already-pending UART overrun: the NEO-M9N emits NMEA at power-up while staged firmware has not yet armed RX. Version 1.0.2 clears the hardware RX/error state before arming, retries that short race once, moves the identity proof before PPS startup, exposes exact failure-stage/HAL counters, and permits explicit GNSS-only recovery. Physical UART, antenna, navigation, and PPS still require the new image and bench evidence.

## RTOS access

`AtlasIO` is the sole UBX parser/transport consumer. It publishes the newest NAV-PVT as `gnss_nav_pvt` and copies interrupt-owned PPS state as `gnss_pps`. Require the relevant GNSS validity bit, then separately validate fix type/flags, accuracy fields, solution age, PPS state, and application navigation criteria. The foreground direct-access example below is an isolated driver example; after scheduling, use `AtlasRtos_GetSnapshot()` so parsing and availability flags retain one owner.

## Board contract

| Item | Atlas value |
|---|---|
| Fitted part | u-blox NEO-M9N, U23 |
| UART | USART1 on PB14/PB15, 38400 baud, 8N1, no flow control |
| Receive path | HAL receive-to-idle interrupt into a 1,024-byte ring (1,023 usable slots) |
| PPS | PA15 / TIM2 channel 1 input capture |
| Receiver reset/safeboot | U23 `RESET_N` and `D_SEL` are not MCU-controlled on rev-0.1; TP12 `SAFEBOOT_N` must remain untouched/high for normal use |
| Timer scale | 1 MHz, 32-bit free-running counter; one count = 1 us |
| Accepted UBX payload | 0 through 512 bytes |
| Board default | 100 ms measurement period (10 Hz), one navigation solution per measurement, UTC time reference, UBX enabled, NMEA disabled, NAV-PVT every solution |

The receiver's documented factory UART is 38400 8N1, with UBX accepted as input and NMEA normally emitted. Atlas first sends a UBX poll without assuming UBX output is already periodic.

## UBX framing and startup

The parser requires:

```text
B5 62 | class | id | length little-endian | payload | CK_A | CK_B
```

Both checksum bytes cover class through payload. A checksum-failing frame is not published. A declared payload above 512 bytes is counted and immediately returns the parser to sync search rather than consuming an untrusted length. Incomplete frames expire after a 250 ms inter-byte gap or 2,000 ms total frame age. These are parser-service timestamps, not per-byte hardware arrival timestamps; delayed service cannot reconstruct exact wire gaps. A UART drop/restart change flushes the ambiguous backlog and resets the parser. Checksums detect ordinary errors; they do not authenticate a sender or guarantee rejection of arbitrary corruption.

Startup proceeds as follows:

1. Call the blocking HAL receive-abort path to disable stale RX interrupts, clear overrun/noise/parity/framing flags, flush RX data, and restore the handle to READY. Discard any ambiguous software-ring backlog.
2. Arm receive-to-idle. If bytes recreate an overrun in the short flush-to-arm window, repeat the entire preflight once; never retry indefinitely.
3. Poll the side-effect-free `UBX-MON-VER` (`0A 04`) immediately, then every 250 ms within one bounded 3,000 ms identity window. This tolerates a receiver protocol engine that is still starting; only a checksum-valid response proves identity.
4. Store printable software/hardware version fields; a missing version response is an identity failure.
5. Start TIM2 input capture. Starting the shared timebase is idempotent: BNO085's running TIM2 is reused without resetting its count. Deferring PPS until after identity makes a communication failure safe to retry.
6. Send one version-0 `UBX-CFG-VALSET` to the RAM layer only.
7. Require `UBX-ACK-ACK` whose payload matches class `0x06`, ID `0x8A`; a matching NAK fails configuration.
8. Poll the same six keys from the RAM layer with version-0 `UBX-CFG-VALGET`.
9. Parse each key using the size encoded in its key ID, reject malformed/duplicate/unknown entries, and require the exact expected value for every requested key.

The RAM transaction writes `CFG-RATE-MEAS`, `CFG-RATE-NAV`, `CFG-RATE-TIMEREF`, UART1 UBX/NMEA output flags, and UART1 NAV-PVT rate. It does not write battery-backed RAM or flash. A write ACK alone is never treated as configuration proof.

## NAV-PVT data contract

`AtlasGnssNavPvt` names every scale in the field name. Important interpretation rules:

- longitude/latitude are signed degrees times `1e7`;
- heights and accuracy are millimetres;
- N/E/D velocity, ground speed, and speed accuracy are millimetres per second;
- motion/vehicle headings are degrees times `1e5`;
- magnetic declination is degrees times `1e2`;
- `fix_type` alone is insufficient—inspect validity and fix/confirmation flags plus accuracy;
- `received_at_ms` is host receipt time, not GNSS measurement time.

```c
AtlasGnssNavPvt nav;

if (AtlasGnss_GetLatestNavPvt(&atlas_board.gnss, &nav, true))
{
    const double latitude_deg = (double)nav.latitude_1e7_deg / 1.0e7;
    /* Gate on flags, fix_type, accuracy, age, and mission-specific validity rules. */
}
```

## PPS contract

Use `AtlasGnss_GetPps()` rather than reading interrupt-owned fields individually. It makes at most three copy attempts and returns false if no pulse exists or coherence cannot be obtained; it does not retry forever. `latest_capture_us` is the low 32 bits of the timer capture. `period_us` uses unsigned subtraction through one wrap. `valid_period` is false until two pulses arrive. A period near 1,000,000 us is expected only after time-pulse configuration/state is understood; pulse presence does not establish valid UTC.

## Health and fault interpretation

| Counter | Meaning |
|---|---|
| `bytes_parsed` / `valid_ubx_frames` | Parser workload and checksum-valid output. |
| `checksum_errors` | Received UBX frame failed CK_A/CK_B. |
| `oversize_frames` | Declared payload exceeded 512 bytes; immediate resynchronization. |
| `frame_timeouts` | An incomplete frame exceeded the inter-byte or whole-frame age bound. |
| `transport_resynchronizations` | UART drop/restart counters changed; buffered ambiguous data and parser state were discarded. |
| `nav_pvt_frames` / `malformed_nav_pvt` | Valid 92-byte solutions vs wrong-length NAV-PVT. |
| `acknowledgements` / `negative_acknowledgements` | Correlated command outcomes. |
| `command_timeouts` | Expected version, ACK, or VALGET response did not arrive within its bound. |
| `configuration_readbacks` | Complete RAM-layer responses that contained every expected key/value exactly once. |
| `configuration_mismatches` | Malformed, missing, duplicate, unknown, or unequal configuration data. |
| `last_failure_stage` / `last_failure_status` | Exact retained startup/configuration/service phase and typed Atlas result. Stage zero means the latest sequence completed. |
| `last_uart_error` / transport `last_hal_status` | STM32 HAL error evidence at the latest failed phase and the most recent transport operation. Preserve hexadecimal bits before power cycling. |
| `pps_capture_started` | True only after identity succeeded and TIM2 channel 1 input capture started. |
| transport `receive_preflights` / `start_retries` | Complete abort/flush preparations and the bounded second-arm count. One normal preflight is expected; a retry is diagnostic, not automatically a failure. |

Failure-stage values published by version 1.0.2 are:

| Value | Phase |
|---:|---|
| 0 | none / latest sequence succeeded |
| 1–2 | UART transport initialization / receive start |
| 3–5 | MON-VER write / service / timeout |
| 6–7 | shared timebase / PPS capture start |
| 8–9 | volatile configuration write / readback |
| 10 | runtime service |

Monitor UART overflow, error and recovery counters alongside parser resynchronizations. A dropped byte invalidates the current stream history; discard stale navigation data and do not interpret successful resynchronization as proof of integrity or a valid fix.

`AtlasBoard_ProbeModule()` treats GNSS differently from other staged probes. A failed identity/startup attempt may be repeated only by another explicit `probe gnss`; the old receiver is stopped before state is cleared. If identity succeeded but RAM configuration failed, a repeat retries only that volatile configuration. A completely successful GNSS remains single-shot and returns `ATLAS_ERROR_STATE` on another probe.

## Bench acceptance

1. Retain passing protocol/review probes, then flash version 1.0.2 and capture 38400 8N1 traffic, MON-VER, VALSET, matching ACK, and complete RAM-layer VALGET response in the actual board startup order.
2. Record software/hardware version and confirm the module/firmware supports every selected key.
3. Decode the captured VALGET independently and prove all six keys match before accepting the configuration.
4. Capture at least 1,000 NAV-PVT frames; verify checksum, exact length, 10 Hz rate, monotonic time-of-week, realistic position/velocity, and no NMEA after configuration.
5. Test no-fix, time-only/degraded, valid 3-D fix, stale data, and antenna-disconnected states.
6. Compare PPS against an independent time reference and record period, jitter, polarity, pulse width, startup state, and behavior through loss/reacquisition.
7. Replay corrupted, truncated, noise-prefixed, and oversize frames through host tests and verify recovery.
8. Fill the UART ring deliberately; verify drop accounting and that no corrupt NAV-PVT is published.
9. If startup fails, retain `failure_stage`, `failure_status`, RX/TX byte counts, preflight/retry counts, UART errors, and HAL status/error before retry or power-cycle. Confirm U23 supplies, TP12, PB14/PB15 idle/traffic, and cross-connections with a DMM/scope before changing firmware assumptions.

## Known limits

- No antenna/open-short diagnostics, constellation/dynamic-model selection, leap-second discipline, or PPS configuration command is implemented.
- U23 reset is not routed to the MCU, so firmware cannot force a receiver cold reset; a genuinely wedged module requires a controlled board power cycle.
- The configuration is intentionally lost on receiver reset/power cycle and is reapplied at MCU boot.
- There is no GNSS week/time conversion or PPS-to-NAV-PVT association yet.
- The 1,024-byte UART ring and 512-byte service budget require scheduling analysis under all enabled message loads.
- GNSS output is not a trusted navigation solution until spoofing/jamming, antenna placement, integrity, and stale-data policies are defined.

## Primary references

- u-blox, [NEO-M9N integration manual, UBX-19014286](https://content.u-blox.com/sites/default/files/NEO-M9N_Integrationmanual_UBX-19014286.pdf).
- u-blox, [M9 SPG 4.04 interface description, UBX-21022436](https://content.u-blox.com/sites/default/files/u-blox-M9-SPG-4.04_InterfaceDescription_UBX-21022436.pdf).
