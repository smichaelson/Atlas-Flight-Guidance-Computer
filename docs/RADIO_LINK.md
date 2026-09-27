# Atlas bench radio connection — 1.4.0

Firmware 1.5.0 retains this acknowledged diagnostic link and adds automatic
[remote sensor telemetry](REMOTE_TELEMETRY.md) in a separate dashboard tab. Pause that
broadcast on both boards before entering guarded AT diagnostics.

The dashboard's **Communications → RFD900x** panel now distinguishes local UART startup
from a verified round trip to another Atlas. Both boards need Bringup or ServoBench
1.4.0. USART3 in both the generated source and CubeMX project uses **57600, 8N1**, matching
the [RFDesign Point-to-Point/SiK factory setting](https://files.rfdesign.com.au/Files/documents/RFD900x%20Peer-to-peer%20V3.X%20User%20Manual%20V1.4.pdf).
The modem's solid green LED establishes RF synchronization; Atlas-level verification
also tests both board UARTs and the peer's application. No modem EEPROM or RF settings
are changed by this update.

## Operation

- Boot starts passive reception. A battery-only board can respond without opening its dashboard.
- **Connect & monitor** performs a test, then issues another every three seconds.
- **Test round trip** supersedes any pending background test and waits up to two seconds
  for a matching response. USB remains responsive; the sensor owner continues sampling.
- A successful test shows peer UID and round-trip milliseconds. A failed UART write and
  a missing peer reply are separate failures. The last explicit test result remains
  visible while monitoring runs.
- **Connected** requires a matching reply less than six seconds old. A failed subsequent
  test clears the connection immediately at its two-second deadline. USB/owner staleness
  suppresses the dashboard's connection claim even if the last stored frame was green.
- **Stop monitoring** clears local connection proof and stops originating tests. The
  board continues to answer valid peer tests. Monitoring is volatile, survives USB
  disconnect, and ends on board reset. It is independent of SW2 and stabilization.
- For local AT identity diagnostics, stop monitoring at both ends and allow the serial
  guard interval. AT access remains blocked while stabilization is enabled.

Both firmware profiles keep their existing output policy. RF traffic cannot enter the
USB console or issue any output, stabilization, storage, settings or bootloader command.
The new protocol carries diagnostic challenges and responses only. ServoBench's saved
calibration, reverse mask, SW2 gates, power checks, PWM timing and standalone operation
are unchanged. The normal application retains its existing radio byte API.

## Wire and timing contract

Each fixed 44-byte frame contains `ATLR`, version 1, type PING=1 or ACK=2, two reserved
zero bytes, sender UID (three uint32), destination UID (three uint32), uint32 sequence,
uint32 originating millisecond timestamp, and CRC-32/ISO-HDLC over the first 40 bytes.
Integers and CRC are little endian. A zero destination is valid for discovery PINGs only.
ACKs must address the full local UID, originate from a different nonzero UID, and match
the outstanding sequence and timestamp before the deadline. CRC provides corruption
detection, **not authentication**; this is not a secure remote-control protocol.

The 44-byte sliding parser handles arbitrary fragmentation/noise without dynamic memory.
Unknown types, versions, reserved bits, CRC failures, local echoes, stale/foreign ACKs
and arbitrary console text do not establish a connection. ACKs are never acknowledged.
There is one reply slot; surplus PINGs are dropped. Replies are limited to one per 100 ms.
Service sends at most one frame per call. A 44-byte UART transfer at 57600 takes about
7.64 ms, with a 20 ms HAL bound; the two-second peer wait is asynchronous. The independent
IO task retains its 5 ms control cadence and existing 100 ms IMU freshness limit.

`hello.radio_link=1` advertises the capability. Radio status includes actual host baud,
monitoring/connected/waiting, last peer/ACK age/RTT, sent/received/replied/acknowledged
counts, timeouts, UART errors/drops, corrupt frames, and a separate last-test sequence,
peer, RTT and result: 0 idle, 1 waiting, 2 acknowledged, 3 timeout, 4 transmit error,
5 cancelled. `ack_age_ms=UINT32_MAX` means no currently retained acknowledgement.

## Verification

Run `Tests/bringup/run_bringup_tests.ps1` for radio protocol, production USB completion,
Python schema/command gates and browser presentation tests, along with existing bench
regressions. Host/service tests and all four build profiles cover shared firmware changes.
See [the review and hardware record](RADIO_LINK_REVIEW.md) for this installation.
