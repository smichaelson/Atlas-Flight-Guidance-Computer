# Peripheral services

**Bench operator? Use [startup](startup.md).** The APIs below are the normal application's contracts. In `ATLAS_BRINGUP=1`, the diagnostic console is their sole client: normal output configuration, PWM/pyro assertions and unbounded GPIO-HIGH commands are rejected. Diagnostic additions are explicitly identified below; the normal profile rejects those additions.

This is the implementation guide for the board services around the sensor drivers. It centralizes ownership, commands, units, failure behavior and safe integration examples. Read [Systems](SYSTEMS.md) for readiness and [RTOS](reference/RTOS.md) for scheduling.

## Major functions and where they live

| Area | Application API | Authoritative implementation |
|---|---|---|
| SD / UTC | `AtlasStorage_Submit()`, `Receive()`, `GetHealth()` | [header](../App/Inc/atlas_storage.h), [owner](../App/Src/atlas_storage.c) |
| USB CDC | `AtlasUsb_Read()`, `Write()`, `GetHealth()` | [header](../App/Inc/atlas_usb.h), [owner](../App/Src/atlas_usb.c) |
| ADC / inputs / outputs | `AtlasIo_GetSnapshot()`, `Submit()`, `Receive()` | [header](../App/Inc/atlas_io.h), [owner/adapter](../App/Src/atlas_io.c), [analog conversion](../App/Src/atlas_analog.c) |
| Pyro policy | Owner-internal `AtlasPyroPolicy_*` | [policy contract](../App/Inc/atlas_pyro_policy.h), [pure state machine](../App/Src/atlas_pyro_policy.c) |
| Expansion buses | `AtlasExpansion_Submit()`, `Receive()`, `ReadUart()`, `GetHealth()` | [header](../App/Inc/atlas_expansion.h), [owner operations](../App/Src/atlas_expansion.c) |
| Radio / BLE commissioning | `AtlasRtos_SubmitCommand()`, `ReadMaintenanceReply()` | [RTOS header](../App/Inc/atlas_rtos.h), [RFD900x](reference/modules/RFD900X.md), [BLE](reference/modules/BLE.md) |
| Emergency inhibition | `AtlasRtos_InhibitOutputs()` | [RTOS gate](../App/Src/atlas_rtos.c) → [immediate output shutdown](../App/Src/atlas_io.c) |

Public declarations and implementations carry `@brief`, `@param`, `@return` and relevant context/safety notes. Do not bypass an owner by calling HAL, a direct device driver, FatFs or the private pyro policy from application code.

## Shared application contract

All normal APIs below require a running task, not an ISR or suspended scheduler. Submit/Receive/Read/Write are nonblocking and copy their data; no caller buffer is retained. Channel indices are **zero-based** and mask bit 0 means connector/channel 1. Voltage is integer **millivolts**; PWM is integer **microseconds**; MCU timestamps are unsigned **milliseconds** with wrap-safe subtraction.

A submit result of `ATLAS_OK` means queued, not executed. Each command family has one designated result consumer. Correlate by its family-specific ticket, not by timing or the latest health field; tickets can wrap. Result queues exert backpressure: a full queue stops further commands, **not continuous monitoring or a launched pyro cutoff**. There is no infinite automatic queue wait or write retry.

`AtlasRtos_InhibitOutputs()` is the exception: a bounded task/ISR/fatal-fault path with no queue, allocation or HAL wait. It permanently inhibits actuation for that boot. A normal queued DISARM/DISABLE is not an emergency mechanism and can wait behind backpressure.

## SD and UTC

`AtlasStorage` is the only FatFs/BSP/disk owner. It has four copied requests and four copied results, each carrying up to 512 bytes. Its priority is below control, sensors, outputs and USB.

| Operation | Contract |
|---|---|
| `ATLAS_STORAGE_MOUNT` | Explicitly discard old state and initialize/mount current media; no formatting |
| `ATLAS_STORAGE_UNMOUNT` | Invalidate mounted state; no retained file handles |
| `ATLAS_STORAGE_READ` | Root 8.3 filename, byte offset, 1–512 bytes; a successful short read means EOF |
| `ATLAS_STORAGE_APPEND` | Open/create without truncation, seek EOF, write, sync, close; preserve prior bytes |
| `ATLAS_STORAGE_SET_UTC` | Explicit validated UTC calendar, 2000–2099; no automatic GNSS/host clock setting |
| `ATLAS_STORAGE_SELF_TEST` | **Bringup only:** create-new `ATLASCHK.TST`, write/sync/close/reopen/compare 1024 bytes; refuse an existing file. `verified_bytes` reports success; payload `length` remains zero |

Filenames have a 1–8 character base and optional 1–3 character extension, using letters, digits, underscore or hyphen. Paths, drive prefixes, spaces, traversal, empty/truncated names and larger requests are rejected. The filename array has room for its NUL terminator.

Normal boot makes a read-only mount attempt; **Bringup waits for an explicit MOUNT**, so even metadata access follows the power checks. Absent media does not fault the flight-control supervisor. J3 has an active-low mechanical detect switch on PD3. Both detect edges invalidate the previous card generation. A card inserted or replaced later needs an **explicit MOUNT**; FatFs cannot silently initialize a different card during a queued file operation.

The port uses synchronous CPU/FIFO polling, **not SDMMC IDMA**. One aligned 512-byte CPU buffer per sector accommodates unaligned callers; DTCM is safe because no SD DMA accesses it. The transfer wait is 250 ms per sector and readiness wait 1000 ms with task yielding. HAL command internals and preemption can extend wall time: these are not an end-to-end logging deadline.

An append may partially reach media before an error, full-card condition, disconnect or failed sync. `result.length` is the reported byte count, not an atomic-commit certificate. Do not blindly retry: duplicate or partially written records are possible. Define a versioned record format, integrity/sequence fields and recovery policy in application code. No stock logger, truncate/delete/format API or filesystem repair is supplied.

UTC validity is recorded in RTC backup register DR0. A partial time/date update invalidates the marker. Reading time then date preserves the STM32 shadow-register snapshot. If time is unknown/invalid, FatFs gets the valid fallback **1980-01-01 00:00:00** and `health.time_valid=false`; it is not a claimed real timestamp. FAT time has two-second resolution.

Example: enqueue bytes only when the application explicitly chooses to log:

```c
#include "atlas_storage.h"
#include <string.h>

/** @brief Queue one application-owned log fragment; does not wait or promise durability.
 * @param data Source bytes; copied before return.
 * @param length Number of bytes, 1-512.
 * @param ticket Optional storage ticket.
 * @return Queue acceptance or typed argument/readiness failure. */
AtlasStatus AtlasExample_Append(const uint8_t *data, uint16_t length, uint32_t *ticket)
{
    if (data == NULL) return ATLAS_ERROR_NULL;
    if (length == 0U || length > ATLAS_STORAGE_DATA_CAPACITY) return ATLAS_ERROR_ARGUMENT;
    AtlasStorageRequest request = {
        .operation = ATLAS_STORAGE_APPEND, .filename = "ATLAS.LOG", .length = length
    };
    memcpy(request.data, data, length);
    return AtlasStorage_Submit(&request, ticket);
}
/* The designated consumer must call AtlasStorage_Receive(), match the ticket,
 * and inspect status, filesystem_result and length. Never retry blindly. */
```

## USB CDC

`AtlasUsb` owns attach/deinit and transmission. VBUS must be stable for at least the nominal 20 ms debounce; initialization rechecks PA9 before exposing the pull-up. Falling VBUS disconnects promptly in the GPIO callback; task-owned teardown leaves SysTick running.

RX uses a 1024-byte ring and retains or drops whole packets, counting loss. Read returns at most 64 bytes. Write accepts 1–64 bytes into a four-packet queue, tagged with the current enumeration session. A private buffer remains valid through the final endpoint completion, including the zero-length packet after a full 64-byte transfer.

Reset/disconnect/reconfiguration invalidates queued old-session data. A TX outstanding for 2000 ms triggers teardown/re-enumeration and is **not replayed**. Completion means an endpoint handshake, not application receipt; timeout/reset can leave the host's received outcome uncertain.

CDC line coding is validated seven-byte metadata, not a command to reconfigure a board UART. DTR/RTS are informational and never arm outputs; SEND_BREAK does not drive a pin. Unsupported/malformed controls stall. The supported ACM requests target interface 0; the data interface is interface 1.

For availability, inspect `health.vbus` and `health.configured`; `health.status` retains the latest controller initialization/timeout result and is not a connection flag. A submit may still race a later unplug and be reported as dropped rather than delivered.

The transport itself does not supply framing, a shell, authentication, flow control based on RX-ring occupancy, or a command parser. The **separate Bringup application** supplies the bounded physical-bench JSON protocol in [startup](startup.md#9-protocol-and-implementation-reference); it is not present in the normal application. USB OTG DMA stays off. Generated ST example VID/PID/strings require an authorized product-identity decision before distribution.

```c
#include "atlas_usb.h"

/** @brief Read a bounded byte fragment for an application parser.
 * @param bytes Destination with at least 64 bytes of storage.
 * @return Bytes copied, or zero if empty/not in a running task.
 * @note Feed a bounded parser; raw bytes must never directly authorize an output. */
size_t AtlasExample_ReadUsb(uint8_t bytes[64])
{
    return AtlasUsb_Read(bytes, 64U);
}
```

## Analog, continuity and inputs

`AtlasOutputs` calibrates ADC1/ADC3 and runs a nominal 5 ms service loop. ADC1 has ten 16-bit ranks in DMA1-accessible AXI SRAM. Only a completed, stopped DMA scan is decoded; failures quarantine that buffer and inhibit outputs. ADC3 explicitly samples VREFINT then temperature as separate single-rank conversions, avoiding overwritten data-register values.

`AtlasIoSnapshot` retains the first ADC3 reference failure as a typed stage (configure, start, overrun, deadline, poll, stop, raw range, VDDA range, or temperature range), the selected channel, raw value, HAL status, and ADC error mask. This diagnostic record is separate from `adc_errors`, which counts external ADC1/DMA scan faults. It is evidence for diagnosis, not permission to accept a rail or bypass fail-closed output inhibition.

| ADC index | Meaning | Nominal divider multiplier |
|---:|---|---:|
| 0 | 3V3 system rail | 2 |
| 1 | PWM rail | 59/19 |
| 2 | 5V system rail | 2 |
| 3 | Protected input | 758/83 |
| 4 | Armed pyro supply | 46/5 |
| 5–9 | Continuity drain equivalents 1–5 | 57/10 |

`analog.valid_mask` governs `millivolts[]`; raw/retained numbers alone cannot authorize anything. Near-full-scale readings are invalid. A fresh factory-calibrated VREF estimate must be 2800–3600 mV; reference age is limited to 250 ms and output-related ADC age to 50 ms. GPIO inputs and PF12 switch are raw logic levels, not debounced user actions.

**Diagnostic-only GPIO:** `ATLAS_IO_BENCH_GPIO` uses `arguments.gpio.channel` (0–6) and `high`. HIGH requests one nominal 1000 ms logic pulse, subject to fresh 3V3 at 3000–3500 mV, current command age/generation, healthy IO, USB configured/DTR and no active pulse or emergency. `high=false` clears **all seven** logic outputs; the channel is ignored. The normal profile returns UNSUPPORTED. In Bringup, expiry/disconnect/rail/emergency checks clear the pulse in the next executing output-owner cycle, not an independent timer cutoff. The diagnostic console is the sole client; its 1–7 channel numbering and fixtures are in [startup](startup.md#general-gpio-loopback).

Continuity classification compares the scaled drain voltage with the qualified armed supply. Three consistent **new** scans establish OPEN/CLOSED. Missing arm voltage, invalid/clipped/stale data, intermediate ratios, firing or scans crossing a pulse edge yield UNKNOWN. Monitoring continues while software-disarmed; calibration/configuration is required before classifying leads. A high indication is not an initiator-resistance measurement or proof of a connected safe load.

```c
#include "atlas_io.h"

/** @brief Copy a current valid 3V3 measurement for diagnostics, without actuation.
 * @param now_ms Current HAL monotonic time.
 * @param millivolts Destination.
 * @return true only for a fresh valid sample; false leaves the destination untouched. */
bool AtlasExample_Read3V3(uint32_t now_ms, uint32_t *millivolts)
{
    AtlasIoSnapshot sample;
    if (millivolts == NULL || !AtlasIo_GetSnapshot(&sample) ||
        sample.status != ATLAS_OK || sample.emergency_latched ||
        (sample.analog.valid_mask & (1U << ATLAS_ANALOG_3V3)) == 0U ||
        (uint32_t)(now_ms - sample.analog.sampled_at_ms) > ATLAS_PYRO_MAX_SAMPLE_AGE_MS ||
        (uint32_t)(now_ms - sample.analog.reference_at_ms) > 250U)
        return false;
    *millivolts = sample.analog.millivolts[ATLAS_ANALOG_3V3];
    return true;
}
```

The snapshot also retains reset flags, PVD-event count, ECC-event count and the first ECC monitor address/failing-word offset/error code. These are per-boot diagnostics, not a persistent crash recorder; an ECC word offset is not directly an absolute RAM address.

## Output configuration and qualification

Stock settings are zero and inhibit actuation. The application must supply `AtlasOutputConfiguration` through `ATLAS_IO_CONFIGURE` and consume its result before submitting dependent commands.

| Field | Required decision |
|---|---|
| `electrical_review_complete` | Explicit responsibility for the assembled board, load, reset states and allowed outputs |
| `pwm_allowed_mask` + `pwm[i]` | Each enabled channel's measured minimum, neutral and maximum within 900–2100 us |
| `gpio_high_allowed_mask` | Which of seven output pins may drive high, with compatible loads |
| `pyro_enabled`, `pyro_cutoff_qualified` | Explicit enable and inert qualification of the actual timer/DMA/electrical cutoff path |
| `arm_minimum_mv`, `arm_maximum_mv` | Qualified armed-supply range; argument bounds are not board/load operating ratings |
| `continuity_open_max_permille`, `continuity_closed_min_permille` | Measured ratio thresholds; software bounds require separated open/closed regions |

No safe mechanical neutral or qualified voltage thresholds are guessed. Do not set the qualification flags merely to get past a check. The [hardware supply limits](reference/HARDWARE.md#power-and-physical-inhibit) and exact connected parts remain controlling constraints.

After the first PWM enable, GPIO HIGH or software-arming attempt, configuration is locked for the boot. Old queued assertions expire after 50 ms and are invalidated by configuration, OFF/disarm, permission-loss and relevant supply transitions. Explicit OFF commands may deassert even with stale/unqualified state. A new enable/arm must be a new deliberate request after the previous command completes.

### PWM and GPIO

PWM1–4 use TIM1; PWM5–8 use TIM3. A 1 MHz counter and period 3003 counts produce nominal 333.0003 Hz. Enabling a channel selects its timer function and loads its explicitly configured neutral. Updates are checked against that channel's limits. Disabling switches the signal pin to GPIO-low; merely stopping a timer is not the shutdown contract.

PWM enable requires a measured 4.8–8.4 V rail. Loss of that range disables PWM and invalidates pending assertions. KST's signal-HIGH specification starts at 3.3 V; the MCU's nominal 3.3 V drive therefore needs measured margin. Servo power remains live even when the signal is disabled.

GPIO output commands specify a zero-based channel and level. HIGH requires qualification, allowed mask and current system health. LOW is always available as a normal queued deassertion. GPIO input masks and the external switch do not have built-in flight/arming semantics.

### Pyro sequence and retry scope

1. Provide qualified settings and wait for valid continuous arm-supply/lead observations.
2. Explicitly submit `ATLAS_IO_PYRO_ARM` and inspect its result. Physical supply presence is not software arming.
3. Submit `ATLAS_IO_PYRO_REQUEST` for one zero-based channel. It must have fresh CLOSED continuity and remaining attempt budget.
4. Observe `snapshot.pyro.phase`, `attempts[]`, `software_armed` and `fault_latched`. Request completion reports **policy acceptance**, not pulse completion or deployment.
5. Disarm deliberately when done; use the immediate inhibition API for an emergency.

TIM6's first update DMA-sets one gate, and its next update 500 ms later DMA-resets all five. Both edges are independent of task progress. Normal abort and emergency first remove the GPIO drive so a late DMA SET cannot raise the gate; normal reuse also requires verified stream quiescence. Peripheral failure, clocks and hardware faults remain qualification concerns.

After completion, the policy waits at least 500 ms OFF plus a tick-phase guard and requires a fresh post-interval observation. CLOSED can retry; OPEN completes the sequence; UNKNOWN cancels/disarms. An initial request whose lead becomes OPEN before launch is canceled, not reported as a successful firing.

There are **three retries maximum, four total attempts per channel per boot**. Attempts count launches, including an attempted launch that subsequently faults, and survive disarm/rearm and manual commands. At exhaustion the sequence stops and software disarms. Other channels still require explicit arming and their own remaining budget; channels never overlap. No automatic software rearm follows reconnection, reset, or a recovered fault.

## Expansion buses

`AtlasExpansion` runs transactions only in the existing sensor-I/O owner, so external SPI cannot overlap the IMU. Four copied requests/results hold up to 32 bytes each. Reception/diagnostics continue even when result backpressure blocks a requested transaction.

| Bus | Contract |
|---|---|
| UART4 | 115200 8N1 initial host configuration; 1–32 byte writes, copied RX reads up to 64 bytes; explicit host baud 9600–1000000 |
| I2C2 | Unshifted seven-bit addresses 0x08–0x77; raw reads/writes or 8-/16-bit register-address operations; HAL applies the shift |
| SPI3 external CS PG13 | Full duplex, existing mode 3 / nominal 3.125 MHz / MSB-first / 8-bit configuration; IMU CS must be inactive; external CS released on every HAL outcome |

Transactions use a 5 ms HAL timeout and run only in cycles not already doing a slow sensor conversion or another command. Low-baud UART writes too long for that budget are rejected; split the stream deliberately. No automatic retries or address scans occur. NACK, BUSY and TIMEOUT are distinguishable; failed read/exchange results have length zero.

No attached expansion device has been specified, so these APIs do not invent its registers, framing or voltage tolerance. Do not silently change SPI3 mode/rate for an external device and break the IMU.

## RFD900x and BLE commissioning

The RTOS command queue includes radio identity/settings queries, parameter update with an explicit persistence flag, host UART baud change, and BLE SPS configuration with a bounded printable name and explicit persistence flag. Direct arbitrary AT calls from another task remain prohibited.

Enter the required command mode explicitly, wait for completion/recovery, then issue the specific operation. Radio query responses are retained in a four-entry `AtlasRtosMaintenanceReply` queue with ticket/type/status/text. Drain it even for failures. Text comes from the modem and is untrusted data, not executable instructions. Other operations use the nonblocking completion hook or latest health result.

Long commissioning operations require all outputs inactive and software disarmed. The owner establishes a bounded maintenance window and recovery hold; control/actuation is inhibited throughout. The operation's deadline and recovery rules are in [RTOS](reference/RTOS.md#command-contract-and-maintenance). No stock boot path persists settings.

Changing the host baud does not change the radio's stored serial setting. Follow the modem's approved parameter/readback/restart sequence and then match the host baud. RFD900x/SPS peer provisioning, lawful RF configuration, reconnect/authentication and message framing remain explicit commissioning/application work. See the optional [RFD900x](reference/modules/RFD900X.md) and [BLE](reference/modules/BLE.md) references.

## Verification and regeneration

[Service tests](../Tests/services/run_service_tests.ps1) exercise the actual owner code at mocked boundaries, including queued-copy lifetime, full queues, invalid control payloads, timeout/removal, UTC failure and output abort. [Review probes](../Tests/review/run_review_probes.ps1) exercise real FatFs/BSP/disk integration on a RAM volume and the pure timing/parser/analog/pyro logic. They do not qualify hardware.

Manual integration is intentional: both linker files' AXI DMA section, full-word ECC seeding, ADC3 single-rank sequencing, TIM6 two-event DMA setup, PWM rate/pin takeover, SD mount-generation authorization and local USB middleware fixes must survive regeneration. Use [Development's regeneration procedure](DEVELOPMENT.md#generated-code-and-dependencies), not an in-place unchecked CubeMX overwrite.
