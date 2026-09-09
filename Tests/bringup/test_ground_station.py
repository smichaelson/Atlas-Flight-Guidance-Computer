"""Adversarial local API, serial ownership, recording and update tests; no hardware."""
import copy
import http.client
import json
from pathlib import Path
import sys
import threading
import time
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/bringup"))
import demo
import ground_station as gs
import update_firmware as fw
from protocol import Decoder, Session


class Serial:
    def __init__(self, data=b""):
        self.data, self.writes, self.closed = data, [], False
    @property
    def in_waiting(self): return len(self.data)
    def read(self, count):
        chunk, self.data = self.data[:count], self.data[count:]
        return chunk
    def write(self, data):
        self.writes.append(data)
        return len(data)
    def close(self): self.closed = True


class WindowsOpeningSerial(Serial):
    """Model Windows purging an early CDC packet during open; never uses a port."""
    def __init__(self, **options):
        super().__init__()
        self.options, self.port, self.opened = options, None, False
        self._dtr, self.rts = True, True
        self.fail_dtr = False
        self.trace = []
        self.startup = (b"\n" + json.dumps(demo.hello()).encode() + b"\n" +
                        json.dumps(demo.status()).encode() + b"\n")

    @property
    def dtr(self): return self._dtr

    @dtr.setter
    def dtr(self, value):
        if self.opened and value and self.fail_dtr:
            raise OSError("setup failed")
        was_high, self._dtr = self._dtr, value
        self.trace.append(("dtr", value))
        if self.opened and value and not was_high:
            self.data += self.startup

    def open(self):
        self.opened = True
        self.trace.append(("open", self.dtr, self.rts))
        # pyserial configures DTR before PurgeComm. With DTR already high, the
        # first endpoint packet can be discarded while the rest is still queued.
        self.data = self.startup[64:] if self.dtr else b'previous-session tail}\n'

    def reset_input_buffer(self):
        self.trace.append(("purge", self.dtr))
        self.data = b""


class StationTests(unittest.TestCase):
    def setUp(self): self.s = gs.Station()
    def live(self):
        self.s.mode, self.s.port, self.s.confirmed = "live", "COM99", True
        self.s.serial = Serial()
        h = demo.hello(); h.update(uid=[1,2,3], software_dfu=True)
        self.s.ingest(h,time.monotonic())
        self.s.ingest(demo.status(),time.monotonic())

    def test_disconnected_has_no_measurements_or_writes(self):
        self.assertIsNone(self.s.snapshot()["status"])
        self.assertFalse(self.s.snapshot()["fresh"])
        with self.assertRaises(ValueError): self.s.action("command",dict(verb="beep"))

    def test_demo_never_opens_serial_or_commands(self):
        with patch("serial.Serial", side_effect=AssertionError("No hardware in demo")):
            self.s.action("demo",{})
            self.s.tick()
            self.assertEqual(self.s.snapshot()["mode"],"demo")
            with self.assertRaises(ValueError): self.s.action("command",dict(verb="probe adxl"))
            with self.assertRaises(ValueError): self.s.action("firmware-update",dict(action_confirmed=True))

    def test_windows_open_purge_cannot_truncate_the_handshake(self):
        with patch.object(self.s, "ports", return_value=[dict(device="MODEL-ONLY")]), \
                patch("serial.Serial", side_effect=WindowsOpeningSerial), \
                patch.object(gs.time, "sleep") as settle:
            self.s.action("connect", dict(port="MODEL-ONLY", confirmed=True))
            device = self.s.serial
            self.s.tick()
        self.assertEqual(self.s.decoder.errors, 0, self.s.decoder.last_error)
        self.assertEqual(self.s.session.hello, demo.hello())
        self.assertTrue(self.s.snapshot()["fresh"])
        self.assertFalse(self.s.session.blocked)
        self.assertEqual(device.writes, [])
        self.assertEqual(device.trace, [("dtr", False), ("open", False, False),
                                        ("purge", False), ("dtr", True)])
        settle.assert_called_once_with(0.1)

    def test_connect_setup_failure_closes_handle_and_stays_disconnected(self):
        for failed_step in ("open", "purge", "dtr"):
            with self.subTest(step=failed_step):
                device = WindowsOpeningSerial()
                if failed_step == "open":
                    device.open = Mock(side_effect=OSError("setup failed"))
                elif failed_step == "purge":
                    device.reset_input_buffer = Mock(side_effect=OSError("setup failed"))
                else:
                    device.fail_dtr = True
                device.close = Mock(side_effect=OSError("removed during cleanup"))
                with patch.object(self.s, "ports", return_value=[dict(device="MODEL-ONLY")]), \
                        patch("serial.Serial", return_value=device), patch.object(gs.time, "sleep"):
                    with self.assertRaisesRegex(OSError, "setup failed"):
                        self.s.action("connect", dict(port="MODEL-ONLY", confirmed=True))
                device.close.assert_called_once()
                self.assertIsNone(self.s.serial)
                self.assertEqual(self.s.mode, "disconnected")
                self.assertFalse(self.s.confirmed)
                self.assertEqual(self.s.handshake_deadline, 0)
                self.assertEqual(device.writes, [])

    def test_incomplete_handshake_times_out_once_without_sending(self):
        for frames in ([], [demo.hello()], [demo.status()]):
            with self.subTest(frames=[f["type"] for f in frames]):
                station = gs.Station()
                station.mode, station.serial = "live", Serial()
                station.serial.data = b"".join(json.dumps(f).encode()+b"\n" for f in frames)
                station.handshake_deadline = time.monotonic() - 1
                station.tick(); station.tick()
                self.assertIn("No complete Atlas handshake", station.session.blocked)
                self.assertEqual(len(station.events), 2 if frames and frames[0]["type"] == "hello" else 1)
                self.assertEqual(sum("within 8 seconds" in e["message"] for e in station.events), 1)
                self.assertEqual(station.serial.writes, [])
                with self.assertRaises(ValueError): station.send("hello")

    def test_completed_handshake_cancels_only_the_startup_deadline(self):
        self.live()
        self.s.handshake_deadline = time.monotonic() + 8
        self.s.tick()
        self.assertEqual(self.s.handshake_deadline, 0)
        self.s.session.received_at -= 10
        self.s.tick()
        self.assertFalse(self.s.session.fresh(time.monotonic()))
        self.assertFalse(self.s.session.blocked)
        with self.assertRaises(ValueError): self.s.send("probe gnss")

    def test_corruption_does_not_recover_on_a_later_good_handshake(self):
        self.s.mode, self.s.confirmed, self.s.serial = "live", True, Serial()
        self.s.handshake_deadline = time.monotonic() - 1
        self.s.serial.data = (b'bad first record\n' + json.dumps(demo.hello()).encode()+b'\n' +
                              json.dumps(demo.status()).encode()+b'\n')
        self.s.tick()
        self.assertIn("Invalid telemetry", self.s.session.blocked)
        self.assertIn("record starts b'bad first record'", self.s.events[-1]["message"])
        self.assertEqual(self.s.handshake_deadline, 0)
        with self.assertRaises(ValueError): self.s.send("probe gnss")
        self.assertEqual(self.s.serial.writes, [])

    def test_rejected_record_preview_is_bounded_and_escaped(self):
        decoder = Decoder()
        decoder.feed(b'\x1b\x00'+b'x'*8000+b'\n')
        self.assertEqual(decoder.errors, 1)
        self.assertIn("\\x1b\\x00", decoder.last_error)
        self.assertNotIn("\x1b", decoder.last_error)
        self.assertLess(len(decoder.last_error), 500)

    def test_moving_demo_preserves_wire_contract(self):
        decoder=Decoder()
        for ms in (10000,12000,999999,0xffffffff):
            self.assertEqual(len(decoder.feed(json.dumps(gs.simulated(ms)).encode()+b"\n")),1)

    def test_explicit_sensor_batch_skips_already_attempted_modules(self):
        self.live(); self.s.session.status["attempted"]=1
        self.s.action("sensors",{})
        self.assertNotIn("probe adxl",self.s.batch)
        self.assertNotIn("probe radio",self.s.batch)
        self.assertNotIn("probe ble",self.s.batch)
        self.s.tick()
        self.assertEqual(len(self.s.serial.writes),1)
        self.s.tick()
        self.assertEqual(len(self.s.serial.writes),1)

    def test_stale_telemetry_cannot_authorize_action(self):
        self.live();self.s.session.received_at=time.monotonic()-10
        with self.assertRaises(ValueError):self.s.send("probe bno")
        self.assertEqual(self.s.serial.writes,[])

    def test_no_new_action_with_pending_command(self):
        self.live();self.s.send("probe bno")
        with self.assertRaises(ValueError):self.s.send("probe gnss")
        self.assertEqual(len(self.s.serial.writes),1)

    def test_no_retry_after_timeout(self):
        self.live();self.s.send("probe bno");self.s.session.pending.began-=50
        self.s.tick();self.s.tick()
        self.assertTrue(self.s.session.blocked)
        self.assertEqual(len(self.s.serial.writes),1)

    def test_malformed_frame_latches_block(self):
        self.live();self.s.serial.data=b'{"type":"status"}\n'
        self.s.tick()
        self.assertTrue(self.s.session.blocked)
        with self.assertRaises(ValueError):self.s.send("probe gnss")

    def test_short_write_latches_uncertainty(self):
        self.live();self.s.serial.write=lambda data:1
        with self.assertRaises(IOError):self.s.send("probe bno")
        self.assertTrue(self.s.session.blocked)

    def test_disconnect_clears_history_and_pending(self):
        self.live();device=self.s.serial;self.s.send("probe bno")
        self.s.disconnect()
        self.assertTrue(device.closed);self.assertIsNone(self.s.snapshot()["status"])
        self.assertEqual(self.s.snapshot()["history"],[])
        self.assertFalse(self.s.confirmed)

    def test_rf_and_media_mutations_need_specific_acknowledgement(self):
        self.live()
        for command in ("radio ping","radio id","sd test","ble profile","gpio 1"):
            with self.assertRaises(ValueError):self.s.action("command",dict(verb=command))
        self.assertEqual(self.s.serial.writes,[])

    def test_arbitrary_and_direct_boot_commands_rejected(self):
        self.live()
        for command in ("pyro fire","reset","bootloader 1 2 3","AT+anything"):
            with self.assertRaises(ValueError):self.s.action("command",dict(verb=command,action_confirmed=True))
        self.assertEqual(self.s.serial.writes,[])

    def test_recording_is_opt_in_and_labelled(self):
        self.s.action("demo",{});self.assertEqual(len(self.s.records),0)
        self.s.action("record",dict(enabled=True))
        self.s.ingest(gs.simulated(11000),time.monotonic())
        self.assertEqual(self.s.records[0]["frame"]["type"],"hello")
        self.assertTrue(all(r["source"]=="demo" for r in self.s.records))
        self.s.action("record",dict(enabled=False))
        with self.assertRaises(ValueError):self.s.action("record",dict(enabled=True))

    def test_capture_limit_does_not_overwrite_old_evidence(self):
        self.s.action("demo",{});self.s.action("record",dict(enabled=True))
        first=copy.deepcopy(self.s.records[0])
        self.s.records.extend([{}]*11999)
        self.s.ingest(gs.simulated(11000),time.monotonic())
        self.assertFalse(self.s.recording);self.assertTrue(self.s.record_full)
        self.assertEqual(self.s.records[0],first)

    def test_boot_requires_advertised_capability_and_matching_uid(self):
        self.live()
        with self.assertRaises(ValueError):self.s.session.request("bootloader 1 2 4",time.monotonic(),True)
        self.s.session.hello.pop("software_dfu")
        with self.assertRaises(ValueError):self.s.session.request("bootloader 1 2 3",time.monotonic(),True)

    def test_boot_rejects_mounted_card_and_active_gpio(self):
        self.live();self.s.session.status["sd"]["mounted"]=1
        with self.assertRaises(ValueError):self.s.session.request("bootloader 1 2 3",time.monotonic(),True)
        self.s.session.status["sd"]["mounted"]=0;self.s.session.status["gpio"]["outputs"]=1
        with self.assertRaises(ValueError):self.s.session.request("bootloader 1 2 3",time.monotonic(),True)

    def test_march_needs_new_firmware_and_specific_confirmation(self):
        self.live()
        with self.assertRaises(ValueError):self.s.action("command",dict(verb="march",action_confirmed=True))
        self.s.session.hello["buzzer_melody"]=True
        self.s.session.status["buzzer"]=dict(playing=0,note=0,notes=33,hz=0,status=0)
        with self.assertRaises(ValueError):self.s.action("command",dict(verb="march"))
        self.s.action("command",dict(verb="march",action_confirmed=True))
        self.assertEqual(self.s.serial.writes,[b"1 march\n"])

    def test_playing_march_blocks_repeat_and_dfu_but_allows_stop(self):
        self.live()
        self.s.session.hello["buzzer_melody"]=True
        self.s.session.status["buzzer"]=dict(playing=1,note=3,notes=33,hz=1568,status=0)
        for verb in ("march","bootloader 1 2 3"):
            with self.assertRaises(ValueError):self.s.session.request(verb,time.monotonic(),True)
        self.s.action("command",dict(verb="stop"))
        self.assertEqual(self.s.serial.writes,[b"1 stop\n"])

    def test_march_telemetry_is_optional_for_old_firmware_but_strict_if_present(self):
        good=demo.status()
        good["buzzer"]=dict(playing=1,note=3,notes=33,hz=1568,status=0)
        decoder=Decoder()
        self.assertEqual(len(decoder.feed(json.dumps(good).encode()+b"\n")),1)
        for key,value in (("playing",2),("note",34),("hz",10001),("notes",0),("status",99)):
            invalid=copy.deepcopy(good);invalid["buzzer"][key]=value
            self.assertEqual(Decoder().feed(json.dumps(invalid).encode()+b"\n"),[])


class ApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=gs.Server(("127.0.0.1",0),gs.Station())
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
    @classmethod
    def tearDownClass(cls): cls.server.shutdown();cls.server.server_close()
    def request(self,path="/api/state",headers=None,body=None):
        connection=http.client.HTTPConnection("127.0.0.1",self.server.server_port,timeout=3)
        h={"X-Atlas-Token":self.server.token,**(headers or {})}
        connection.request("POST" if body is not None else "GET",path,body,h)
        response=connection.getresponse();result=response.status,response.read(),dict(response.getheaders())
        connection.close();return result
    def test_local_ui_and_csp(self):
        code,body,headers=self.request("/")
        self.assertEqual(code,200);self.assertIn(b"Atlas",body)
        self.assertIn("frame-ancestors 'none'",headers["Content-Security-Policy"])
    def test_api_token_required(self):self.assertEqual(self.request(headers={"X-Atlas-Token":"wrong"})[0],403)
    def test_dns_rebinding_host_rejected(self):self.assertEqual(self.request(headers={"Host":"evil.example"})[0],403)
    def test_cross_site_request_rejected(self):self.assertEqual(self.request(headers={"Sec-Fetch-Site":"cross-site"})[0],403)
    def test_cross_origin_post_rejected(self):self.assertEqual(self.request("/api/demo",{"Origin":"https://evil.example","Content-Type":"application/json"},"{}")[0],403)
    def test_form_content_rejected(self):self.assertEqual(self.request("/api/demo",{"Content-Type":"text/plain"},"{}")[0],400)
    def test_big_body_rejected(self):self.assertEqual(self.request("/api/demo",{"Content-Type":"application/json"}," "*4097)[0],400)
    def test_non_object_rejected(self):self.assertEqual(self.request("/api/demo",{"Content-Type":"application/json"},"[]")[0],400)
    def test_fixed_asset_paths(self):self.assertEqual(self.request("/../../README.md")[0],404)
    def test_valid_demo_request(self):
        self.assertEqual(self.request("/api/demo",{"Content-Type":"application/json"},"{}")[0],200)


class UpdateTests(unittest.TestCase):
    def test_uid_parse_requires_address_and_three_words(self):
        self.assertEqual(fw.parse_uid("0x1FF1E800 : 00000001 00000002 DEADBEEF"),[1,2,0xdeadbeef])
        for text in ("00000001 00000002 DEADBEEF","0x1FF1E804 : 00000001 00000002 DEADBEEF","0x1FF1E800 : 00000001"):
            with self.assertRaises(ValueError):fw.parse_uid(text)
    @patch.object(fw,"programmer_path",return_value=Path("official-ST-tool.exe"))
    def test_wrong_uid_never_writes(self,_):
        with patch.object(fw,"run_cli",side_effect=["USB Port : USB1\nSerial number : ABCDEF123456", "Device ID : 0x450\n0x1FF1E800 : 00000004 00000002 00000003"]) as run:
            with self.assertRaises(ValueError):fw.program(Path("not-read"),[1,2,3],report=lambda _:None)
            self.assertEqual(run.call_count,2)
            self.assertTrue(all("-d" not in call.args[1] for call in run.call_args_list))
    @patch.object(fw,"programmer_path",return_value=Path("official-ST-tool.exe"))
    def test_multiple_dfu_devices_never_writes(self,_):
        with patch.object(fw,"run_cli",return_value="USB Port : USB1\nUSB Port : USB2") as run:
            with self.assertRaises(ValueError):fw.program(Path("not-read"),[1,2,3],report=lambda _:None)
            self.assertEqual(run.call_count,1)
    @patch.object(fw,"programmer_path",return_value=Path("official-ST-tool.exe"))
    @patch.object(fw,"verify",return_value=dict(program_file="frozen.hex"))
    def test_failed_verify_never_starts_application(self,*_):
        with patch.object(fw,"run_cli",side_effect=["USB Port : USB1\nSerial number : ABCDEF123456", "Device ID : 0x450\n0x1FF1E800 : 00000001 00000002 00000003\n0x1FF1E7FE : 92","Download complete; verification failed"]) as run:
            with self.assertRaises(RuntimeError):fw.program(Path("fixture"),[1,2,3],report=lambda _:None)
            self.assertTrue(all("-s" not in call.args[1] for call in run.call_args_list))
    @patch.object(fw,"programmer_path",return_value=Path("official-ST-tool.exe"))
    @patch.object(fw,"verify",return_value=dict(program_file="frozen.hex"))
    def test_success_verifies_before_start(self,*_):
        with patch.object(fw,"run_cli",side_effect=["USB Port : USB1\nSerial number : ABCDEF123456", "Device ID : 0x450\n0x1FF1E800 : 00000001 00000002 00000003\n0x1FF1E7FE : 92","Download verified successfully","Application started"]) as run:
            result=fw.program(Path("fixture"),[1,2,3],report=lambda _:None)
            self.assertTrue(result["flash_verified"])
            self.assertFalse(result["application_reconnected"])
            self.assertIn("-v",run.call_args_list[2].args[1]);self.assertIn("-s",run.call_args_list[3].args[1])
            identity_args=run.call_args_list[1].args[1]
            self.assertEqual(identity_args[identity_args.index("-r32")+2],"12")
            self.assertTrue(all("sn=ABCDEF123456" in call.args[1] for call in run.call_args_list[1:]))
            self.assertTrue(all("-e" not in call.args[1] and "-ob" not in call.args[1] for call in run.call_args_list))

    @patch.object(fw,"programmer_path",return_value=Path("official-ST-tool.exe"))
    @patch.object(fw,"verify",return_value=dict(program_file="frozen.hex"))
    def test_old_unknown_rom_and_initial_install_require_cold_boot(self,*_):
        for revision,start in (("90",True),("FF",True),("",True),("92",False)):
            identity="Device ID : 0x450\n0x1FF1E800 : 00000001 00000002 00000003"
            if revision: identity+="\n0x1FF1E7FE : "+revision
            with self.subTest(revision=revision,start=start),patch.object(fw,"run_cli",side_effect=["USB Port : USB1\nSerial number : ABCDEF123456",identity,"Download verified successfully"]) as run:
                result=fw.program(Path("fixture"),[1,2,3],report=lambda _:None,start_application=start)
                self.assertTrue(result["flash_verified"])
                self.assertTrue(result["power_cycle_required"])
                self.assertFalse(result["application_start_requested"])
                self.assertEqual(run.call_count,3)

    @patch.object(fw,"programmer_path",return_value=Path("official-ST-tool.exe"))
    def test_missing_dfu_serial_never_reads_or_writes(self,_):
        with patch.object(fw,"run_cli",return_value="USB Port : USB1") as run:
            with self.assertRaises(ValueError):fw.program(Path("fixture"),[1,2,3],report=lambda _:None)
            self.assertEqual(run.call_count,1)


if __name__=="__main__":unittest.main(verbosity=2)
