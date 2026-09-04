"""Offline dashboard/framing/artifact tests. No serial import or hardware access.

Major cases cover malformed frames, exact commands, stale/reset/disconnect state,
uncertain completion, sample ages/units, SD preservation and image verification.
"""
import json
import ast
import hashlib
import os
from pathlib import Path
import re
import sys
import subprocess
import struct
import tempfile
import types
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bringup"))
import card_check
import demo
import image_check
from protocol import Decoder, Session, age_ms, observations, valid_command, validate


class ProtocolTests(unittest.TestCase):
    """Pure tests of the actual desktop protocol code."""

    def connected(self):
        """@brief Build a fresh, synthetic session. @return Test session."""
        session = Session()
        session.accept(demo.hello(), 1.0)
        session.accept(demo.status(), 1.0)
        return session

    @unittest.skipUnless(os.environ.get('ATLAS_CONSOLE_TEST_EXE'), 'Use run_bringup_tests.ps1 for C/Python interoperability')
    def test_actual_firmware_console(self):
        """@brief Validate real C dispatcher assertions and normal/extreme serialized frames."""
        process = subprocess.run([os.environ['ATLAS_CONSOLE_TEST_EXE']], capture_output=True, check=True, timeout=10)
        decoder = Decoder()
        frames = decoder.feed(process.stdout)
        self.assertEqual(decoder.errors, 0, decoder.last_error)
        self.assertEqual(len(frames), 3)
        self.assertEqual([f['type'] for f in frames], ['hello', 'status', 'status'])
        self.assertEqual(frames[-1]['ble']['model'], '\x01' * 96)
        self.assertIn('nt', frames[-1]['mmc'])
        self.assertIn('mag_nt', frames[-1]['bno'])
        self.assertIn('health', frames[-1]['bno'])
        self.assertIn('led', frames[-1])
        self.assertTrue(all(len(line) < 8192 for line in process.stdout.splitlines()))

    def test_fragments_and_bounds(self):
        """@brief Fragment every byte; reject oversized frames and recover after LF."""
        data = json.dumps(demo.hello()).encode() + b"\r\n"
        decoder = Decoder()
        output = []
        for byte in data:
            output += decoder.feed(bytes([byte]))
        self.assertEqual(output, [demo.hello()])
        self.assertEqual(decoder.feed(b"x" * 9000 + data), [])
        self.assertEqual(len(decoder.buffer), 0)
        self.assertEqual(decoder.errors, 1)
        self.assertEqual(decoder.feed(data), [demo.hello()])

    def test_invalid_frames(self):
        """@brief Reject duplicate keys, NaN, bad schemas, wrong profile and unsafe outputs."""
        decoder = Decoder()
        for line in (b'[]\n', b'{"type":"hello","type":"status"}\n', b'{"type":NaN}\n', b'\xff\n'):
            self.assertEqual(decoder.feed(line), [])
        for path, value in ((('schema',), 2), (('schema',), True), (('profile',), 'normal'), (('gpio', 'pwm'), 1),
                            (('power', 'mv'), [1]), (('bno', 'count'), [True] * 4),
                            (('bno', 'health'), {'interrupts': 0}),
                            (('bno', 'health', 'failure_stage'), 10),
                            (('bno', 'health', 'intn_low'), 2), (('led', 'gates'), 8),
                            (('led', 'inhibited'), 0), (('led', 'commanded'), 1),
                            (('gnss', 'failure_stage'), 11), (('gnss', 'hal_status'), 4),
                            (('power', 'ref_stage'), 10), (('power', 'ref_channel'), 2),
                            (('power', 'ref_hal_status'), 4),
                            (('tasks', 'fault'), -1), (('gnss', 'version'), 12),
                            (('mmc', 'nt'), [0x80000000, 0, 0])):
            frame = demo.status()
            parent = frame
            for component in path[:-1]:
                parent = parent[component]
            parent[path[-1]] = value
            with self.assertRaises(ValueError):
                validate(frame)
        frame = demo.status()
        del frame['baro']['pa']
        with self.assertRaises(ValueError):
            validate(frame)
        for replacement in (False, None):
            hello = demo.hello()
            if replacement is None:
                del hello['led_inhibited']
            else:
                hello['led_inhibited'] = replacement
            with self.assertRaises(ValueError):
                validate(hello)

    def test_exact_commands(self):
        """@brief No host arbitrary text, pyro/PWM, scan, erase or impossible date."""
        for verb in ('probe adxl', 'gpio 0', 'gpio 7', 'utc 2024 2 29 12 0 0', 'i2c 80 0', 'sd test'):
            self.assertTrue(valid_command(verb), verb)
        for verb in ('pyro fire', 'pwm 1', 'sd format', 'led 1', 'led 8', 'gpio -1', 'probe adxl\n1 beep',
                     'utc 2026 2 29 12 0 0', 'i2c 0x50 0', 'i2c 120 0', 'i2c 8 256', 'ble arbitrary'):
            self.assertFalse(valid_command(verb), verb)

    def test_fixture_contracts_match_firmware_and_guide(self):
        """@brief Cross-check exact payloads/lengths in C, desktop fixture and operator guide."""
        root = Path(__file__).resolve().parents[2]
        source = (root / 'App/Src/atlas_bringup.c').read_text()
        strings = {}
        for key in ('BENCH_SD_TEXT', 'BENCH_TEST_TEXT'):
            match = re.search(r'#define\s+' + key + r'\s+("[^\n]+")', source)
            self.assertIsNotNone(match)
            strings[key] = ast.literal_eval(match.group(1)).encode('ascii')
        self.assertEqual(strings['BENCH_SD_TEXT'], card_check.READ_TEXT)
        self.assertEqual(len(strings['BENCH_SD_TEXT']), 23)
        self.assertEqual(strings['BENCH_TEST_TEXT'], b'ATLAS_LINK_TEST_1\r\n')
        self.assertEqual(len(strings['BENCH_TEST_TEXT']), 19)
        guide = (root / 'docs/startup.md').read_text(encoding='utf-8')
        self.assertIn('**23 verified bytes**', guide)
        self.assertIn('(19 bytes)', guide)

    def test_handshake_confirmation_and_stale(self):
        """@brief A COM port alone never authorizes testing; no implicit command."""
        s = Session()
        self.assertIsNone(s.pending)
        with self.assertRaises(ValueError):
            s.request('probe adxl', 1, True)
        s = self.connected()
        with self.assertRaises(ValueError):
            s.request('probe adxl', 1, False)
        with self.assertRaises(ValueError):
            s.request('probe adxl', 5, True)
        self.assertEqual(s.request('probe adxl', 1.5, True), b'1 probe adxl\n')
        with self.assertRaises(ValueError):
            s.request('probe lsm', 1.6, True)

    def test_reply_matching_and_timeout(self):
        """@brief Wrong replies cannot release pending requests; timeout latches uncertainty."""
        s = self.connected()
        s.request('sd test', 1.1, True)
        reply = dict(type='reply', id=2, status=0, name='OK', detail='', verified_bytes=1024)
        s.accept(reply, 2)
        self.assertIsNotNone(s.pending)
        reply['id'] = 1
        s.accept(reply, 2)
        self.assertIsNone(s.pending)
        self.assertEqual(s.last_reply['command'], 'sd test')
        s.request('sd mount', 2.1, True)
        s.check_timeout(48)
        self.assertIn('UNKNOWN', s.blocked)
        with self.assertRaises(ValueError):
            s.request('sd test', 48, True)
        s.accept(dict(reply, id=2), 49)
        self.assertTrue(s.blocked)  # A late reply cannot silently clear uncertainty.

    def test_reset_wrap_and_remote_pending(self):
        """@brief Distinguish uint32 tick wrap from reboot; fence unfinished remote work."""
        s = self.connected()
        s.accept(demo.status(100), 2)
        self.assertIn('restarted', s.blocked)
        s = Session()
        s.accept(demo.hello(), 1)
        s.accept(demo.status(0xFFFFFFF0), 1)
        s.accept(demo.status(20), 2)
        self.assertEqual(s.blocked, '')
        self.assertEqual(age_ms(20, 0xFFFFFFF0), 36)
        frame = demo.status(30)
        frame['pending_id'] = 77
        s.accept(frame, 2.1)
        with self.assertRaises(ValueError):
            s.request('sd test', 2.2, True)

    def test_observation_semantics(self):
        """@brief Freshness/units/fix flags are visible; no automatic hardware PASS."""
        frame = demo.status()
        rows = observations(validate(frame))
        self.assertEqual(len(rows), 14)
        self.assertIn('21.000', rows[2][3])
        self.assertIn('uT', rows[2][3])
        self.assertIn('I2C reads/writes', rows[4][3])
        self.assertIn('RGB firmware-inhibited', rows[13][3])
        self.assertIn('UART RX/TX', rows[8][3])
        self.assertIn('ADC3 none on temperature', rows[12][3])
        self.assertIn('external ADC errors 0', rows[12][3])
        self.assertEqual(rows[8][1], 'RESPONDING / NO 3D FIX')
        frame['adxl']['t'] = 0
        self.assertEqual(observations(frame)[0][1], 'STALE')
        frame['attempted'] = 0
        self.assertEqual(observations(frame)[0][1], 'NOT TESTED')
        self.assertTrue(all('PASS' not in row[1] for row in rows))

    def test_invalid_measurements_are_not_responding(self):
        """@brief Null/nonrepresentable measurements and unqualified ADC are not green."""
        for section, key, row in (('adxl', 'mg', 0), ('lsm', 'mdps', 1), ('mmc', 'nt', 2),
                                  ('bno', 'accel_mm_s2', 4), ('bno', 'q_ppm', 7)):
            frame = demo.status()
            frame[section][key][0] = None
            self.assertEqual(observations(validate(frame))[row][1], 'INVALID DATA')
        for key in ('pa', 'temp_cc'):
            frame = demo.status()
            frame['baro'][key] = None
            self.assertEqual(observations(validate(frame))[3][1], 'INVALID DATA')
        frame = demo.status()
        frame['power']['valid'] &= ~1
        self.assertEqual(observations(validate(frame))[12][1], 'INVALID DATA')
        frame['power']['available'] = False
        self.assertEqual(observations(validate(frame))[13][1], 'UNAVAILABLE')

    def test_fixture_preservation(self):
        """@brief Fixtures refuse overwrites and catch corruption/truncation/extra data."""
        # Retain a unique fixture directory like the C suites. Windows sandbox
        # identities may not share TemporaryDirectory's restrictive creation ACL.
        path = Path(os.environ.get('ATLAS_BRINGUP_TEST_DIR', tempfile.gettempdir())) / ('card-' + uuid.uuid4().hex)
        path.mkdir()
        if path.is_dir():
            fixture = card_check.prepare(path)
            self.assertEqual(fixture.read_bytes(), b'ATLAS SD READ TEST v1\r\n')
            with self.assertRaises(FileExistsError):
                card_check.prepare(path)
            expected = bytes((i * 37 + (i >> 8) * 13 + 0xA5) & 255 for i in range(1024))
            target = path / card_check.TEST_NAME
            target.write_bytes(expected)
            self.assertEqual(card_check.verify(path), 1024)
            for corrupt in (expected[:-1], expected + b'x', b'x' + expected[1:]):
                target.write_bytes(corrupt)
                with self.assertRaises(ValueError):
                    card_check.verify(path)

    def test_hex_bounds(self):
        """@brief Bad checksums, data outside flash and trailing records are rejected."""
        data = ':020000040800F2\n:080000000000022009000008C5\n:00000001FF\n'
        # Generate the data-record checksum rather than depending on a copied literal.
        prefix = bytes.fromhex('080000000000022009000008')
        data = ':020000040800F2\n:' + prefix.hex() + f'{(-sum(prefix)) & 255:02x}\n:00000001FF\n'
        self.assertEqual(len(image_check.hex_data(data)), 8)
        for bad in (data.replace('F2', 'F3'), data.replace('0800F2', '0000FA'), data + ':00000001FF\n'):
            with self.assertRaises(ValueError):
                image_check.hex_data(bad)

    def test_image_manifest_and_vectors(self):
        """@brief Offline wrong-profile/hash/name/vector/content checks reject bad images."""
        path = Path(os.environ.get('ATLAS_BRINGUP_TEST_DIR', tempfile.gettempdir())) / ('image-' + uuid.uuid4().hex)
        path.mkdir()

        def record(kind, address, payload):
            """@brief Encode one checksum-correct test Intel HEX record."""
            body = bytes([len(payload), address >> 8, address & 255, kind]) + payload
            return ':' + body.hex() + f'{(-sum(body)) & 255:02x}\n'

        def install(stack=0x20020000, reset=0x08000009, hex_mismatch=False):
            """@brief Create synthetic artifacts only in the unique inert test directory."""
            binary = struct.pack('<II', stack, reset) + bytes(range(24))
            encoded = bytearray(binary)
            if hex_mismatch:
                encoded[-1] ^= 1
            ihex = (record(4, 0, b'\x08\x00') + record(0, 0, encoded) + record(1, 0, b'')).encode('ascii')
            elf = b'\x7fELF' + bytes(60)
            for name, data in (('bin', binary), ('hex', ihex), ('elf', elf)):
                (path / ('Atlas-Bringup.' + name)).write_bytes(data)
            manifest = dict(schema=1, profile='bringup', target='STM32H743ZIT6', build_type='Debug',
                            flash_base='0x08000000', binary='Atlas-Bringup.bin', binary_bytes=len(binary),
                            binary_sha256=hashlib.sha256(binary).hexdigest(), hex='Atlas-Bringup.hex',
                            hex_sha256=hashlib.sha256(ihex).hexdigest(), elf_sha256=hashlib.sha256(elf).hexdigest())
            (path / 'Atlas-Bringup.manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
            return manifest

        def store_manifest(manifest):
            """@brief Alter only the synthetic manifest for one negative test."""
            (path / 'Atlas-Bringup.manifest.json').write_text(json.dumps(manifest), encoding='utf-8')

        manifest_path = path / 'Atlas-Bringup.manifest.json'
        install()
        evidence = image_check.verify(manifest_path)
        self.assertTrue(evidence['verified_offline'])
        self.assertFalse(evidence['hardware_tested'])
        for key, value in (('profile', 'normal'), ('schema', True), ('target', 'STM32F4'),
                           ('flash_base', '0x00000000'), ('binary', '../wrong.bin'),
                           ('hex', '../wrong.hex'), ('binary_bytes', 31),
                           ('binary_sha256', '0' * 64), ('hex_sha256', '0' * 64),
                           ('elf_sha256', '0' * 64)):
            manifest = install()
            manifest[key] = value
            store_manifest(manifest)
            with self.assertRaises(ValueError, msg=key):
                image_check.verify(manifest_path)
        for stack, reset in ((0x24000000, 0x08000009), (0x20020000, 0x08000008),
                             (0x20020000, 0x08000021), (0x20020000, 0x08010001)):
            install(stack, reset)
            with self.assertRaises(ValueError):
                image_check.verify(manifest_path)
        install(hex_mismatch=True)
        with self.assertRaises(ValueError):
            image_check.verify(manifest_path)
        store_manifest([])
        with self.assertRaises(ValueError):
            image_check.verify(manifest_path)

    def test_ui_serial_lifecycle_with_inert_model(self):
        """@brief Exercise actual UI connect/send/partial-write/unplug paths without a port."""
        import tkinter as tk
        import dashboard

        class SerialModel:
            """Inert serial boundary; never delegates to the OS or imports pyserial."""
            def __init__(self, **options):
                """@brief Capture configuration without creating an OS handle."""
                self.options, self.port = options, None
                self.dtr = self.rts = False
                self.writes = []
                self.events = []
                self.short_write = self.fail_close = False

            def open(self):
                """@brief Record simulated open ordering and DTR state."""
                self.events.append(('open', self.dtr, self.port))

            def reset_input_buffer(self):
                """@brief Record simulated backlog discard."""
                self.events.append(('discard_old_input',))

            def write(self, data):
                """@brief Retain modeled bytes. @return Full or injected short count."""
                self.writes.append(data)
                return 3 if self.short_write else len(data)

            def close(self):
                """@brief Model successful close or removal-time OS failure."""
                self.events.append(('close',))
                if self.fail_close:
                    raise OSError('modeled unplug during close')

        root = tk.Tk()
        root.withdraw()
        app = dashboard.Dashboard(root)
        try:
            with patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=SerialModel)}), \
                    patch.object(dashboard.messagebox, 'askyesno', return_value=True), \
                    patch.object(dashboard.messagebox, 'showerror'), \
                    patch.object(dashboard.messagebox, 'showwarning'):
                app.port_map = {'MODEL ONLY': 'INERT-NOT-A-COM-PORT'}
                app.port.set('MODEL ONLY')
                app.connect()
                port = app.serial
                self.assertIsInstance(port, SerialModel)
                self.assertEqual(port.events, [('open', False, 'INERT-NOT-A-COM-PORT'), ('discard_old_input',)])
                self.assertEqual(port.writes, [])  # Merely opening sends no command.
                self.assertTrue(port.dtr)
                self.assertEqual(port.options['timeout'], 0)
                self.assertEqual(port.options['write_timeout'], 0.2)
                now = dashboard.time.monotonic()
                app.session.accept(demo.hello(), now)
                app.session.accept(demo.status(), now)
                app.confirmed.set(True)
                app.send('sd test')
                self.assertEqual(port.writes, [b'1 sd test\n'])
                app.send('sd test')
                self.assertEqual(len(port.writes), 1)  # Pending requests are never repeated.
                app.session.accept(dict(type='reply', id=1, status=0, name='OK', detail='', verified_bytes=1024), now)
                port.short_write = True
                app.send('probe adxl')
                self.assertIn('UNKNOWN', app.session.blocked)
                app.send('probe adxl')
                self.assertEqual(len(port.writes), 2)
                port.fail_close = True
                app.disconnect()
                self.assertIsNone(app.serial)
                self.assertIsNone(app.session.pending)
                self.assertFalse(app.confirmed.get())
        finally:
            app.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
