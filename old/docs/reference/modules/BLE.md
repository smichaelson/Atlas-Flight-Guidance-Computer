# NINA-B112 Bluetooth Low Energy

[Documentation hub](../../README.md) · [Current system readiness](../../SYSTEMS.md)

## Major firmware functions

| Function | Contract |
|---|---|
| `AtlasBle_Init()` | Starts USART6, guarantees normal boot straps, requests command mode over wired DSR, and proves two-way AT plus model/firmware identity. |
| `AtlasBle_Command()` | Sends one bounded AT line, tolerates unrelated/unsolicited lines, and requires terminal `OK` or classifies terminal error/timeout. |
| `AtlasBle_ConfigureSps()` | Stages name, Peripheral role, SPS server, Data-mode startup, and wired command-mode behavior; queries every queryable profile item and keeps NVM persistence explicit. |
| `AtlasBle_EnterCommandMode()` / `EnterDataMode()` | Require the documented unsolicited `OK` from the wired DSR transition before probing with `AT`, and require terminal `OK` for `ATO1`, respectively. |
| `AtlasBle_WriteData()` / `ReadData()` | Move opaque transparent bytes only while the driver is explicitly in data mode. |
| `AtlasBle_IsDtrAsserted()` | Reads the module-to-MCU active-low status/connection-control output. |

Source: [`atlas_ble.h`](../../../App/Inc/atlas_ble.h) and [`atlas_ble.c`](../../../App/Src/atlas_ble.c).

## Validation state

The AT engine, exact SPS profile readback, wired mode transitions, mismatch rejection, fail-closed response-buffer overflow, and persisted post-restart verification pass deterministic host tests; the complete target image compiles without warnings. Commands and settings have been reconciled against u-connectXpress R55 for NINA-B112-05B software 7.0.0. No physical module/firmware transcript, SPS connection, flow-control trace, independent power cycle, or NVM cycle has yet been executed.

The protocol tests use a non-RTOS time mock. Separate timing probes now exercise the corrected scheduler-aware minimum-delay helper, including the 50 ms request after `ATO1` and unfavorable tick phase ([R02](../../REVIEW_REPORT.md#r02-rtos-waits-do-not-guarantee-minimum-device-delays)). Measure the actual wire gap and peer behavior before accepting runtime payload transitions.

## RTOS access

Ordinary startup leaves the identified module in Command mode. Application code may queue `BLE_ENTER_DATA_MODE` or `BLE_ENTER_COMMAND_MODE`, queue up to 64 payload bytes per `BLE_WRITE`, and receive Data-mode bytes with `AtlasRtos_ReadBle()`. Mode/write completion is asynchronous: correlate the returned ticket with `AtlasRtos_CommandCompleted()` or `AtlasRtosHealth`. A mode change publishes `maintenance_active` and then a bounded `sensor_recovery_active` interval; control must remain inhibited during both. The 1,024-byte RTOS receive stream counts new-byte drops and does not define message framing. Runtime SPS commissioning uses the explicit `ATLAS_RTOS_COMMAND_BLE_CONFIGURE_SPS` request with a copied name and `persist` flag. It requires all outputs off/disarmed and has a 35 s maintenance bound followed by sensor recovery. No persistence is requested at ordinary boot. See the [central commissioning workflow](../../PERIPHERALS.md#rfd900x-and-ble-commissioning); never call `AtlasBle_ConfigureSps()` directly from a runtime hook.

## Board contract

| Item | Atlas value |
|---|---|
| Fitted part | NINA-B112-05B, U21; u-connectXpress 7.0.0 is the matching production software in the current u-blox data sheet |
| UART | USART6, 115200 baud, 8N1, hardware RTS/CTS |
| Reset | PD4 `BLE_RESET_N`, active low |
| Mode straps | PG5 `BLE_SWITCH1`, PG4 `BLE_SWITCH2`; both held high for normal application boot |
| Module input | PG6 board net `BLE_DSR` drives NINA `UART_DSR` |
| Module output | PE1 board net `BLE_DTR` reads NINA `UART_DTR`, active-low interpretation |
| Receive path | Receive-to-idle interrupt and 1,024-byte ring |
| Reset timing | 20 ms asserted, then 800 ms boot wait |

Never drive both switch pins low during reset: u-blox assigns that combination to bootloader/factory-recovery behavior. `AtlasBle_Reset()` always forces both high first.

## Ordinary boot behavior

`AtlasBle_Init()` is deliberately nonpersistent:

1. Start UART buffering.
2. Assert the module's UART_DSR input low, hold normal switch levels, pulse RESET_N, and wait.
3. Drive UART_DSR high. With the documented/default `AT&D1` profile this moves a saved Data-mode startup back to command mode without placing `+++` into a live data stream.
4. Require terminal `OK` from `AT`.
5. Require nonempty responses from `AT+CGMM` and `AT+CGMR`.

No `AT&W`, `AT+CPWROFF`, factory command, role change, advertising change, or NVM write occurs in this path.

An AT identity response is therefore not proof of an active SPS server, advertising, peer connection or usable application data path. The installed firmware/version and saved `AT&D`/UART profile must be recorded during commissioning.

## SPS configuration contract

`AtlasBle_ConfigureSps(ble, name, persist)` accepts 1 through 29 printable ASCII characters excluding quote, comma, and backslash. It stages:

| Command | Intended state |
|---|---|
| `AT+UBTLN="name"` | BLE local name |
| `AT+UBTLE=2` | Peripheral role |
| `AT+UDSC=0,0`, then `AT+UDSC=0,6` | Replace server 0 with the u-blox Serial Port Service |
| `AT+UMSM=1` | Start in ordinary transparent Data mode, not Extended Data Mode |
| `AT&D1` | DSR asserted-to-deasserted transition enters command mode |

It then queries name, role, server 0, and start mode and requires exact expected response lines. A mismatch returns `ATLAS_ERROR_PROTOCOL` and increments `configuration_mismatches`. u-connectXpress defines `AT&D` as a set-only command, so the accepted `OK` is recorded but is not mislabeled as a register readback. Its effect is proven when a later low-to-high UART_DSR transition first produces the documented unsolicited terminal `OK` and a subsequent fresh `AT` also receives terminal `OK`.

With `persist == false`, no NVM command or reboot occurs; the module remains in command mode and the staged profile is not claimed to survive power loss. u-blox requires a reboot before a changed Bluetooth server configuration takes effect, so this nonpersistent path is a dry-run/readback tool, not proof of an active SPS server. With `persist == true`, the function issues `AT&W`, then `AT+CPWROFF`, and performs a normal hardware reset. It then drives the wired UART_DSR transition, requires its unsolicited `OK`, proves command mode with `AT`, repeats the name/role/server/start-mode readbacks against the saved profile, and uses `ATO1` to return to Data mode. Success therefore means that this post-restart transaction passed; it still does not prove an independent power cycle, RF connection, or remote payload delivery. This is the only normal driver path that writes the BLE module's NVM.

## Mode and payload use

```c
/* Isolated pre-scheduler driver test only; runtime uses the queued command.
 * Explicit NVM commissioning operation; preserve the test transcript. */
AtlasStatus status = AtlasBle_ConfigureSps(&atlas_board.ble, "Atlas-FGC", true);

/* Success includes post-restart readback and a verified return to data mode. */
if (status == ATLAS_OK)
{
    static const uint8_t bytes[] = {0xA5, 0x01, 0x00, 0x5A};
    (void)AtlasBle_WriteData(&atlas_board.ble, bytes, sizeof(bytes), 100U);
}

/* Temporarily leave data mode without injecting an escape sequence into payload. */
(void)AtlasBle_EnterCommandMode(&atlas_board.ble);
/* ... bounded read-only AT queries ... */
(void)AtlasBle_EnterDataMode(&atlas_board.ble);
```

The byte APIs do not define telemetry framing. Add a version, length, message type, byte order, integrity field, and authentication/security policy above this driver.

## Health and fault interpretation

| Counter | Meaning |
|---|---|
| `resets` | Normal hardware resets requested by the driver. |
| `commands_sent`, `command_ok`, `command_errors`, `command_timeouts` | AT transaction outcomes. |
| `response_overflows` | A line or caller response buffer could not hold all text; the transaction fails with `ATLAS_ERROR_OVERFLOW` rather than accepting truncated evidence. |
| `configuration_mismatches` | A readback did not equal the staged SPS setting. |
| `mode_transitions` | Verified command/data transition count. |
| `mode_transition_failures` | A wired transition lacked its terminal `OK`, its follow-up `AT` failed, or an `ATO1` transition failed. |
| `data_bytes_written` / `data_bytes_read` | Successful transparent-byte movement, not remote delivery proof. |

Monitor the underlying UART `dropped_bytes`, HAL error, and restart counters. Hardware RTS/CTS follows UART peripheral capacity, not the software-ring occupancy; a blocked I/O consumer can still lose bytes. It cannot repair a wrong pin mapping or a dropped stream.

## Bench acceptance

1. Confirm normal switch levels, reset pulse, 115200 8N1, and both RTS/CTS directions with a logic analyzer.
2. Capture `AT`, model, and firmware identity; verify the fitted unit reports software compatible with every configured command.
3. Run `ConfigureSps(..., false)` first; retain all write acknowledgements and the four supported profile readbacks, and confirm no power cycle/NVM write.
4. With an approved recovery path, run the persistent path once; power-cycle independently and confirm name, role, server, Data-mode startup, DSR entry to command mode, and `ATO1` return.
5. Connect a known BLE central to SPS; transfer binary vectors containing NUL, CR/LF, and `+++`, in both directions, with sequence/CRC at the test layer.
6. Apply sustained throughput and forced CTS pauses; require no UART drop and no reordered bytes.
7. Test absent module, wrong baud, reset during command, disconnect during data, response overflow, and recovery without selecting bootloader/factory mode.
8. Review pairing, bonding, encryption, authorization, key storage, and command exposure before any operational use.

## Known limits

- Boot probing proves the AT channel, not an RF link or remote delivery.
- SPS security/pairing policy is not configured by this baseline.
- No unsolicited-event parser, connection handle state machine, advertising policy, or reconnection policy exists.
- `AT&D1` has no query syntax. The driver proves it operationally during a subsequent Data-to-Command transition; an externally modified persistent profile can prevent ordinary command-mode recovery and therefore requires the documented hardware recovery procedure.
- The firmware does not automatically persist configuration because unexpected NVM changes are difficult to recover remotely.

## Primary references

- u-blox, [NINA-B1 series data sheet, UBX-15019243 R19](https://content.u-blox.com/sites/default/files/NINA-B1_DataSheet_UBX-15019243.pdf).
- u-blox, [u-connectXpress AT commands manual, UBX-14044127 R55](https://content.u-blox.com/sites/default/files/u-connectXpress-ATCommands-Manual_UBX-14044127.pdf?hash=undefined).
