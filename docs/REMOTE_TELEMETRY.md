# Remote Atlas telemetry — firmware 1.5.0

Both bench profiles now initialize onboard sensors and broadcast a complete read-only
snapshot about once per second. Connect either board by USB and open **Remote Atlas**.
Its peer needs battery power and a paired RFD900x, but no USB cable, dashboard, SW2 change
or remote command. Both boards require 1.5.0. The existing Communications round-trip test
remains available and has its own explicit acknowledgements.

The remote view includes direct accelerometers/gyroscope, magnetometer, barometer,
BNO085 reports/quaternion, GNSS position/fix/PPS, ten ADC channels with raw readings and
voltages, temperature, storage presence/errors, SW2, PWM/stabilization state and faults.
Missing sensors and an indoor GPS without a fix are reported as unavailable, not zeros
or invented positions. Local and remote measurements have separate envelopes and UI
state; remote data cannot set local outputs, answer local commands or refresh USB status.

Sensor startup runs once before stabilization IMU samples are published. Allow startup
to finish before using SW2. Existing saved calibration, switch interlocks, independent
5 ms IO task, 100 ms IMU stale limit and output policies remain in place. GNSS configuration
uses the existing volatile RAM setup; modem settings and SD contents are not changed.
An optional sensor failure does not stop other sensor reporting.

## Packet and redundancy contract

This is an application packet protocol above the modem byte transport, not a forwarded
serial terminal. Every snapshot is 512 bytes, divided into eight 64-byte data shards and
two 64-byte repair shards. Any eight of the ten checked shards recover the snapshot.
Each full 100-byte packet contains:

| Bytes | Field |
|---|---|
| 0–3 | `ATLT` magic |
| 4 / 5 | Packet version 1 / telemetry type 1 |
| 6 / 7 | Shard index 0–9 / data-shard count 8 |
| 8–19 | Source MCU UID, three little-endian uint32 words |
| 20–27 | Random 64-bit source boot identifier |
| 28–31 | uint32 snapshot sequence, wrapping naturally |
| 32–95 | 64 payload/repair bytes |
| 96–99 | CRC-32/ISO-HDLC over bytes 0–95 |

The snapshot has an additional CRC-32 over its first 508 bytes. The first repair shard
is the XOR of all data shards. The second is their weighted sum over GF(256), polynomial
`0x11d`, with coefficients `2^index`. Two missing or CRC-rejected shards can be repaired;
an incomplete or bad whole-snapshot CRC never updates displayed measurements.

The bounded sliding parser resynchronizes after byte loss, insertion, truncation or
noise. Assembly accepts reordered shards, rejects duplicate/older sequences and expires
after 1500 ms. One peer UID is retained for the gateway boot; a different board is counted
as a foreign sender. Peer reboot changes clear the old partial snapshot, while four recent
retired boot identifiers reject delayed packets. CRC/UIDs are integrity/identity checks,
not cryptographic authentication. This protocol contains no control messages.

Boot identifiers use the STM32H743 RNG with a total 20 ms initialization/read bound and
clock/seed error checks. A failure disables local broadcasting while reception remains
available. The temporary HSI48/RNG setup restores its clock selection and does not change
USB or sensor clocks. See ST [RM0433, RNG](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

Packets are paced at least 35 ms apart; a complete batch is nominally 1000 UART bytes per
second per direction at 57600 8N1. One UART send is at most 100 bytes, bounded to 25 ms;
there is no peer-response wait, heap allocation, retransmission queue or old-data backlog.
Unfinished outgoing batches older than one second are abandoned in favor of a new snapshot.
Explicit ping/ACK work has priority over a telemetry send in that owner iteration.

## Snapshot schema

All values are little-endian 32-bit words. Signed measurements use two's complement;
`INT32_MIN` means an unavailable scaled measurement. Reserved words are zero. The stable
field offsets are in [atlas_telemetry.h](../App/Inc/atlas_telemetry.h); the laptop mapping is
in [remote_telemetry.py](../tools/bringup/remote_telemetry.py). No C structure padding is sent.

| Words | Contents |
|---|---|
| 0–4 | `TLS1` schema magic, source uptime, firmware version, profile, attempted sensor mask |
| 5–32 | Init results, successful sample counts, error counts, sample statuses and times |
| 33–47 | Direct acceleration in mg, gyro in millidegrees/s, temperature in centidegrees C, magnetic field in nT, pressure in Pa |
| 48–69 | BNO report counts/times/accuracy; acceleration mm/s², gyro milliradians/s, magnetic nT, quaternion W/X/Y/Z in millionths |
| 70–80 | GNSS receive time/count, fix/flags/satellites, lat/lon in 1e-7 degrees, altitude/accuracy in mm, time of week and PPS diagnostics |
| 81–105 | ADC time/count/status/validity, VDDA, die temperature, ten mV channels, packed raw uint16 counts, error/reset/power/fault evidence |
| 106–123 | GPIO/SW2/PWM, stabilization state/up vector, eight pulse widths, SD state/errors, sensor service errors, TX count and startup flag |
| 124–126 / 127 | Reserved / whole-snapshot CRC |

USB publishes a separate `type: remote`, schema 1 record approximately every half second.
It carries gateway/peer identities, boot ID, sequence, receive/assembly times, transport
counters and the hex snapshot. The laptop verifies the snapshot CRC again and validates
the schema before decoding it. Invalid remote records are counted separately from local
USB/control errors. Session recordings retain both record types.

## Freshness and link measurements

**Receiving** requires a fresh gateway/USB envelope and a complete snapshot received less
than 3500 ms ago. Losing either side hides measurements. Each sensor additionally carries
its own validity/count and source timestamp. The UI displays age at capture; its freshness
gate also includes elapsed gateway time and assembly duration. There is no synchronized
one-way clock measurement, so unmeasured time before the first packet is not an exact
sensor-age/latency guarantee. Round-trip latency remains available in Communications.

- **Application packet loss:** missing shard slots divided by expected slots for finalized
  batches. Ten slots per observed batch; forward sequence gaps account for completely
  missing batches when reception resumes. This is not the modem's internal RF loss rate.
- **CRC rejection rate:** rejected CRC candidates divided by checked valid plus CRC-rejected
  framed packets. Bytes that destroy the header may instead become sequence/shard loss.
  This is not a bit error rate.
- **Good/lost snapshots:** complete CRC-checked snapshots versus finalized unrecoverable or
  wholly missing batches. **Repaired** counts parity use; a late original packet can still
  arrive after repair, so repairs do not necessarily imply a finalized missing packet.
- **Throughput:** locally received valid packet bytes, completed useful payload bytes and
  packets per second over a roughly one-second window. Rates drop to zero during silence.
- **Other counters:** duplicate/old/foreign/unsupported packets, observed boot sessions,
  whole-batch CRC failures, UART errors/dropped bytes, host rejections, TX errors and
  abandoned outgoing batches. Hardware RSSI/SNR/bit error rate are not fabricated.

Counters are saturating uint32 totals since the gateway boot. Reconnecting USB does not
reset them. Packet loss before the first observed sequence cannot be measured.

**Pause local broadcast** and **Resume local broadcast** affect only the USB-connected
board and do not change its reception or saved settings. Boot resumes broadcasting.
For guarded AT modem diagnostics, pause telemetry and stop radio monitoring on both
boards first. These diagnostic controls do not change output/stabilization settings.

## Verification

Run `Tests/bringup/run_bringup_tests.ps1`. Tests exercise every one/two-shard erasure,
every packet bit, fragmentation/truncation, bad outer and whole-snapshot CRCs, reboot,
sequence/tick wrap, pacing, stale expiry, gateway/source isolation and UI freshness.
The real C snapshot formatter is decoded by the production Python validator. See
[the three-review and hardware record](REMOTE_TELEMETRY_REVIEW.md) for installation evidence.
