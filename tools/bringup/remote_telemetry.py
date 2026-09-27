"""Read-only remote snapshot schema; never feeds local commands or local status.

Major functions: validate_remote checks the gateway envelope; decode_snapshot
checks the whole-snapshot CRC again and maps the fixed 128-word wire schema.
Offsets are specified by App/Inc/atlas_telemetry.h and docs/REMOTE_TELEMETRY.md.
"""
from __future__ import annotations
import binascii
import re
import struct

STATS = ('rx_packets rx_bytes crc_errors header_errors duplicates old_packets '
         'foreign_packets sessions expected_packets missing_packets good_batches '
         'lost_batches recovered_batches recovered_packets batch_crc_errors '
         'tx_packets tx_bytes tx_errors tx_batches tx_aborted rx_bps payload_bps rx_pps').split()


def uint(value, maximum=0xffffffff):
    return type(value) is int and 0 <= value <= maximum


def decode_snapshot(encoded: str) -> dict | None:
    """Decode exactly one CRC-checked source snapshot; zeros mean no snapshot yet."""
    if not isinstance(encoded, str) or not re.fullmatch(r'[0-9A-Fa-f]{1024}', encoded):
        raise ValueError('Invalid remote snapshot length/encoding')
    raw = bytes.fromhex(encoded)
    if not any(raw):
        return None
    w = struct.unpack('<128I', raw)
    if w[0] != 0x31534c54 or binascii.crc32(raw[:508]) != w[127]:
        raise ValueError('Remote snapshot CRC/schema mismatch')
    if (w[3] > 1 or w[4] > 255 or any(n > 13 for n in w[5:17]+w[25:29]) or
            any(w[124:127]) or w[123] > 1 or w[83] & ~0x10f or (w[83] & 255) > 13 or
            w[84] > 1023 or w[106] & ~0x3ff7f7f or w[107] & ~0xff070f0f or
            (w[107] >> 24) > 9 or ((w[107] >> 16) & 255) > 6 or
            w[115] & ~0xf03 or (w[115] >> 8) > 13 or w[117] > 13 or
            w[72] & 0xff000000 or (w[72] & 255) > 5):
        raise ValueError('Invalid remote snapshot fields')
    def signed(n):
        return None if n == 0x80000000 else n if n < 0x80000000 else n-0x100000000
    def vector(offset, length=3):
        return [signed(n) for n in w[offset:offset+length]]
    def halfwords(offset, count):
        return [(w[offset+i//2] >> (16*(i%2))) & 65535 for i in range(count)]
    accuracy = [(w[56] >> (8*i)) & 255 for i in range(4)]
    if any(n > 3 for n in accuracy):
        raise ValueError('Invalid remote BNO accuracy')
    return dict(ms=w[1], version=f'{w[2]>>16}.{(w[2]>>8)&255}.{w[2]&255}',
                profile='servo_bench' if w[3] else 'bringup', attempted=w[4], init=list(w[5:17]),
                count=list(w[17:21]), errors=list(w[21:25]), sample_status=list(w[25:29]),
                adxl=dict(t=w[29], mg=vector(33)),
                lsm=dict(t=w[30], mg=vector(36), mdps=vector(39), temp_cc=signed(w[42])),
                mmc=dict(t=w[31], nt=vector(43)),
                baro=dict(t=w[32], pa=signed(w[46]), temp_cc=signed(w[47])),
                bno=dict(count=list(w[48:52]), t=list(w[52:56]), accuracy=accuracy,
                         accel_mm_s2=vector(57), gyro_mrad_s=vector(60), mag_nt=vector(63),
                         q_ppm=vector(66, 4), io_errors=w[118], protocol_errors=w[119]),
                gnss=dict(t=w[70], frames=w[71], fix=w[72]&255, flags=(w[72]>>8)&255,
                          sv=(w[72]>>16)&255, lat_e7=signed(w[73]), lon_e7=signed(w[74]),
                          h_msl_mm=signed(w[75]), hacc_mm=w[76], tow_ms=w[77],
                          pps_count=w[78], pps_us=w[79], crc_errors=w[80], uart_errors=w[120], dropped=w[121]),
                power=dict(t=w[81], count=w[82], status=w[83]&255, available=bool(w[83]&256),
                           valid=w[84], vdda_mv=w[85], temp_c=signed(w[86]), mv=list(w[87:97]),
                           raw=halfwords(97,10), adc_errors=w[102], reset_flags=w[103], power_events=w[104]),
                fault=w[105], gpio=dict(inputs=w[106]&127, outputs=(w[106]>>8)&127,
                                       switch=(w[106]>>16)&1, pwm=(w[106]>>17)&255, armed=(w[106]>>25)&1),
                stabilization=dict(enabled=w[107]&1, active=(w[107]>>1)&1, calibrated=(w[107]>>2)&1,
                                   saved=(w[107]>>3)&1, reverse_mask=(w[107]>>8)&15,
                                   state=(w[107]>>16)&255, reason=w[107]>>24, up_mg=vector(108)),
                pulse_us=halfwords(111,8), sd=dict(card=w[115]&1, mounted=(w[115]>>1)&1,
                                               status=w[115]>>8, errors=w[116]),
                service=w[117], tx_packets=w[122], startup=w[123])


def validate_remote(frame: dict) -> dict:
    """Validate transport evidence, then derive data solely from the checked payload."""
    if frame.get('type') != 'remote' or not uint(frame.get('schema'), 1) or frame['schema'] != 1:
        raise ValueError('Unsupported remote telemetry schema')
    for key in ('ms', 'owner_ms', 'sequence', 'received_ms', 'assembly_ms', 'uart_errors', 'uart_dropped'):
        if not uint(frame.get(key)):
            raise ValueError('Invalid remote ' + key)
    for key in ('available', 'tx_ready', 'streaming'):
        if not uint(frame.get(key), 1):
            raise ValueError('Invalid remote ' + key)
    for key, count in (('gateway', 3), ('peer', 3), ('boot', 2)):
        value = frame.get(key)
        if not isinstance(value, list) or len(value) != count or not all(uint(n) for n in value):
            raise ValueError('Invalid remote identity')
    s = frame.get('stats')
    if not isinstance(s, dict) or any(not uint(s.get(k)) for k in STATS):
        raise ValueError('Invalid remote counters')
    data = decode_snapshot(frame.get('payload_hex'))
    if (not any(frame['gateway']) or frame['peer'] == frame['gateway'] or
            s['missing_packets'] > s['expected_packets'] or s['recovered_batches'] > s['good_batches'] or
            frame['streaming'] > frame['tx_ready'] or (frame['available'] and
                (data is None or not any(frame['peer']) or not any(frame['boot']) or not s['good_batches'] or
                 frame['assembly_ms'] >= 1500))):
        raise ValueError('Remote telemetry lacks coherent evidence')
    return dict(frame, data=data)
