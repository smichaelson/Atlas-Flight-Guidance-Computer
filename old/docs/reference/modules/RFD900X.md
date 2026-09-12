# External RFD900x Radio Interface

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasRfd900x_Init()` | Starts the USART3 byte transport without transmitting, entering command mode, or changing the modem. |
| `AtlasRfd900x_Write()` / `Read()` | Move opaque transparent-link bytes only outside command mode. |
| `AtlasRfd900x_EnterCommandMode()` | Requires at least 1.1 s of measured UART silence before sending `+++`, then waits for terminal `OK`. |
| `AtlasRfd900x_Command()` | Executes a bounded local `AT...` transaction and captures nonterminal response lines. |
| `AtlasRfd900x_ReadIdentity()` / `ReadSettings()` | Read `ATI` and `ATI5` only after deliberate command-mode entry. |
| `AtlasRfd900x_SetParameter()` | Writes `ATSn=value`, queries `ATSn?`, compares the active value, and optionally issues `AT&W`. |
| `AtlasRfd900x_ExitCommandMode()` | Requires acknowledged `ATO` before returning to transparent state. |
| `AtlasRfd900x_ReconfigureHostBaud()` | Changes only the MCU UART to a baud already configured in the modem. |

Source: [`atlas_rfd900x.h`](../../../App/Inc/atlas_rfd900x.h) and [`atlas_rfd900x.c`](../../../App/Src/atlas_rfd900x.c).

## Correct technology boundary

The owner confirmed on **2026-09-01** that an **RFD900x will be connected to J9**, the connector labeled “LoRa.” The existing RFD900x target is correct. It is a serial FHSS modem with the supported RFDesign Point-to-Point/SiK command interface, not a Semtech LoRa/LoRaWAN transceiver. Exact installed radio firmware/settings still require verification. Use `RFD900x` or `radio` in new protocol documentation; retain legacy net names only when tracing the schematic/generated pins.

## Validation state

The driver target-builds without warnings. Deterministic host tests cover substantive non-echo identity/settings responses, echo-tolerant S-register readback and mismatch rejection. The real-time escape guard and full modem startup/link are source-reviewed, not exercised by those tests. No external modem was accessed for identity, baud, firmware, RF link, regional configuration, thermal, range or throughput validation.

## RTOS access

`AtlasIO` owns the USART3 transport. Queue up to 64 transparent bytes per `RADIO_WRITE`, consume received bytes with `AtlasRtos_ReadRadio()`, and correlate asynchronous tickets/results. The 1,024-byte RTOS stream counts overflow and still requires a versioned length/sequence/integrity layer. Guarded enter/exit command-mode requests publish `maintenance_active` and then a bounded `sensor_recovery_active` interval; control must remain inhibited during both. Runtime maintenance requests now cover `RADIO_READ_IDENTITY`, `RADIO_READ_SETTINGS`, `RADIO_SET_PARAMETER`, and `RADIO_HOST_BAUD`. Identity/settings text is retained with ticket/type/status in the bounded `AtlasRtosMaintenanceReply` queue; drain it even on failure. `persist` must be explicit for an NVM write. All maintenance requires outputs off/disarmed and inhibits control through sensor recovery. Use the [central commissioning workflow](../../PERIPHERALS.md#rfd900x-and-ble-commissioning); entering command mode never transfers driver ownership to the application.

## Board contract

| Item | Atlas value |
|---|---|
| Connection | J9 external radio header |
| Host UART | USART3, current generated default 115200 baud, 8N1, no hardware flow control |
| Logic | 3.3 V UART |
| Power | Direct `5V_SYS` on the header; not behind the small accessory-port current limiters |
| Receive path | Receive-to-idle interrupt into a 1,024-byte ring (1,023 usable slots) |
| Normal boot | Transparent transport only; modem presence is not proven |
| SiK factory serial default | Commonly 57600 baud, 8N1; this conflicts with the current host default until the installed modem is configured or the host is deliberately changed |

RFDesign specifies high peak current at maximum transmit power. Verify supply, cable drop, grounding, antenna, heat sinking, and regional modem variant before attaching or transmitting.

## Transparent link contract

The driver carries bytes, not messages. `AtlasRfd900x_Write()` returning `ATLAS_OK` proves only that the local UART accepted the bytes. It does not prove RF synchronization, remote receipt, packet integrity, ordering across resets, or application acknowledgement.

Define a protocol above the driver with at least:

- sync/version/type/length;
- sequence number and monotonic timestamp;
- explicit byte order and units;
- CRC or stronger integrity check;
- authentication/replay policy where commands are accepted;
- retry/acknowledgement, duplicate handling, rate limits, and link-loss behavior.

## Guarded command mode

SiK uses the in-band `+++` escape. If application payload is active, mishandling it can interrupt or reinterpret the link. Atlas therefore:

1. checks the most recent receive or transmit time and refuses with `ATLAS_ERROR_BUSY` unless at least 1,100 ms have elapsed;
2. flushes stale buffered input;
3. transmits exactly three plus bytes with no line ending;
4. sends nothing else while the modem enforces the post-escape guard and waits up to 1,600 ms for `OK`;
5. marks command mode only after that response.

The caller must first quiesce every producer of radio bytes. A time check cannot prevent another task/ISR from transmitting unless higher-level ownership is exclusive.

```c
/* Isolated pre-scheduler driver test; runtime uses queued maintenance requests. */
char identity[160];
char settings[ATLAS_RFD900X_RESPONSE_CAPACITY];

if (AtlasRfd900x_EnterCommandMode(&atlas_board.radio) == ATLAS_OK)
{
    (void)AtlasRfd900x_ReadIdentity(&atlas_board.radio,
                                    identity,
                                    sizeof(identity));
    (void)AtlasRfd900x_ReadSettings(&atlas_board.radio,
                                    settings,
                                    sizeof(settings));
    (void)AtlasRfd900x_ExitCommandMode(&atlas_board.radio);
}
```

## Parameter and baud workflow

`AtlasRfd900x_SetParameter()` affects only the local modem. It reads the exact S-register back before returning success; `persist == true` then writes the current profile to EEPROM. It does not issue `ATZ`, because some changes can break the link or UART immediately after reboot.

For serial speed, change the modem's S1 while at the old baud, verify and persist it, decide when to reboot/exit according to the exact SiK firmware, then call `AtlasRfd900x_ReconfigureHostBaud()` at the controlled transition. Never guess the new host rate. Parameters marked as required to match must be coordinated on the remote modem first, as RFDesign recommends.

## Health and fault interpretation

| Counter | Meaning |
|---|---|
| `payload_bytes_written` / `payload_bytes_read` | Local transparent transport activity only. |
| `command_entries` / `commands_sent` | Deliberate local configuration activity. |
| `command_timeouts` / `command_errors` | Missing terminal response or modem `ERROR`. |
| `guard_rejections` | Entry refused because the UART had not been silent long enough. |
| `malformed_responses` | `ATI` or `ATI5` returned no substantive line after optional command echo. |
| `configuration_mismatches` | `ATSn?` did not parse or equal the requested value. |

Also inspect the UART ring drop and recovery counters; a dropped payload byte must be detected by the future application framing layer.

## Bench acceptance

1. Identify exact modem model, hardware revision, region lock, SiK/other firmware type, firmware version, antenna, and legal operating region.
2. Begin at the modem's known serial rate. If unknown, use a controlled baud-detection/recovery procedure outside flight software.
3. Prove local transparent loopback and paired-modem bidirectional binary transfer before any setting change.
4. Capture the full 1.1 s pre-guard, `+++`, post-guard, `OK`, `ATI`, `ATI5`, and `ATO` sequence.
5. Retain settings from both ends and reconcile serial speed, air speed, NETID, frequency/channel settings, encryption, MAVLink framing, and regional constraints.
6. Test one noncritical S-register with `persist == false`; verify write/readback and restoration. Perform persistent/reboot tests only with a recovery fixture.
7. Run sustained bidirectional load above the planned peak, with message-layer sequence/CRC, while monitoring voltage, temperature, UART drops, RF resync, latency, and loss.
8. Test antenna absent/mismatched only under a manufacturer-approved low-power setup; do not transmit at high power into an unsafe load.

## Known limits

- Normal boot initializes only the MCU transport; even a logical startup-pass LED state does not prove modem presence or an RF peer link.
- No modem GPIO/flow-control wiring is used by the current driver.
- Remote `RT...` commands are intentionally absent.
- No encryption key handling, RF configuration policy, thermal supervisor, link quality parser, or regulatory configuration is supplied.
- SiK firmware families are not necessarily command/configuration compatible; qualify the exact installed firmware.

## Primary references

- RFDesign, [RFD900x Point-to-Point/SiK V3.x user manual, V1.4](https://files.rfdesign.com.au/Files/documents/RFD900x%20Peer-to-peer%20V3.X%20User%20Manual%20V1.4.pdf).
- RFDesign, [RFD x-series modem product information](https://rfdesign.com.au/modems/).
